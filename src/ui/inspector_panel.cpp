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
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "scene/light_properties.hpp"
#include "scene/scene_document.hpp"
#include "scene/transform_pivot.hpp"
#include "core/release_features.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_inspector_property_help.hpp"
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_scale_link_button.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/ui_context.hpp"
#include "viewport/viewport_settings.hpp"
#include "ui/ui_continuous_edit_tracker.hpp"
#include "ui/ui_panel.hpp"

#include <QApplication>
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
#include <QHoverEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPersistentModelIndex>
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
#include <QToolButton>
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
constexpr int kInspectorNumericDispatchIntervalMs = 16;
constexpr float kInspectorLightRangeMin = 0.001f;
constexpr float kInspectorLightRangeMax = 100.0f;
constexpr int kInspectorMinimumValueColumnWidth = 96;
constexpr int kInspectorSectionBodyPadding = 8;
constexpr int kInspectorAxisRowPaddingRole = Qt::UserRole + 1;
constexpr int kInspectorPropertyInsetRole = Qt::UserRole + 2;
constexpr int kInspectorAxisRowSpacingRole = Qt::UserRole + 3;
constexpr int kInspectorPropertyTopPaddingRole = Qt::UserRole + 4;
constexpr int kInspectorPropertyBottomPaddingRole = Qt::UserRole + 5;
constexpr int kInspectorRetainedLabelRole = Qt::UserRole + 6;
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

BoolAggregate aggregate_selectability(const SceneDocument &document,
                                      const std::vector<EntityId> &selected) {
  BoolAggregate result;
  for (const EntityId id : selected) {
    if (const SelectableComponent *selectability = document.selectable(id)) {
      result.has_value = true;
      result.all_true = result.all_true && selectability->selectable;
      result.any_true = result.any_true || selectability->selectable;
    }
  }
  return result;
}

bool same_float(float left, float right) {
  return std::fabs(left - right) <= 0.0001f;
}

std::array<bool, 3> mixed_location_axes(const SceneDocument &document,
                                        const std::vector<EntityId> &selected,
                                        const TransformComponent &primary) {
  std::array<bool, 3> mixed{false, false, false};
  for (const EntityId id : selected) {
    if (const TransformComponent *transform = document.transform(id)) {
      mixed[0] =
          mixed[0] || !same_float(transform->location.x, primary.location.x);
      mixed[1] =
          mixed[1] || !same_float(transform->location.y, primary.location.y);
      mixed[2] =
          mixed[2] || !same_float(transform->location.z, primary.location.z);
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
  explicit InspectorElidedLabel(QString text, QString help,
                               QWidget *parent = nullptr,
                               bool follow_layout_direction = false)
      : QLabel(parent), full_text_(std::move(text)), help_(std::move(help)),
        follow_layout_direction_(follow_layout_direction) {
    setTextFormat(Qt::PlainText);
    setAccessibleName(full_text_);
    update_directional_alignment();
    update_elided_text();
  }

protected:
  void changeEvent(QEvent *event) override {
    QLabel::changeEvent(event);
    if (event->type() == QEvent::LayoutDirectionChange) {
      update_directional_alignment();
    }
  }

  void resizeEvent(QResizeEvent *event) override {
    QLabel::resizeEvent(event);
    update_elided_text();
  }

private:
  void update_directional_alignment() {
    if (follow_layout_direction_) {
      setAlignment(Qt::AlignAbsolute | Qt::AlignVCenter |
                   (layoutDirection() == Qt::RightToLeft ? Qt::AlignRight
                                                         : Qt::AlignLeft));
    }
  }

  void update_elided_text() {
    const QString elided =
        fontMetrics().elidedText(full_text_, Qt::ElideRight, width());
    QLabel::setText(elided);
    setToolTip(elided == full_text_ ? help_
                                   : full_text_ + QStringLiteral("\n\n") + help_);
  }

  QString full_text_;
  QString help_;
  bool follow_layout_direction_ = false;
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

/// Routes numeric cancellation through the Inspector's preview owner.
class InspectorDoubleSpinBox final : public QtDoubleSpinBox {
public:
  explicit InspectorDoubleSpinBox(QWidget *parent = nullptr)
      : QtDoubleSpinBox(parent) {}

  std::function<void()> cancel_handler;

protected:
  void keyPressEvent(QKeyEvent *event) override {
    if (event != nullptr && event->key() == Qt::Key_Escape &&
        cancel_handler != nullptr) {
      if (!qt_install_numeric_spin_box(*this).cancel_interaction()) {
        const QPointer<InspectorDoubleSpinBox> guard(this);
        const auto cancel = cancel_handler;
        cancel();
        if (guard != nullptr) {
          const QSignalBlocker blocker(this);
          clearFocus();
        }
      }
      event->accept();
      return;
    }
    QtDoubleSpinBox::keyPressEvent(event);
  }
};

/// Keeps the first and last visible properties inset from their section edges.
std::pair<int, int> inspector_row_vertical_padding(
    const QTreeWidgetItem &item, int inner_padding) {
  const QTreeWidgetItem *first = nullptr;
  const QTreeWidgetItem *last = nullptr;
  if (const auto *section = item.parent()) {
    for (int index = 0; index < section->childCount(); ++index) {
      const auto *child = section->child(index);
      if (!child->isHidden()) {
        if (first == nullptr) {
          first = child;
        }
        last = child;
      }
    }
  }
  return {first == &item ? kInspectorSectionBodyPadding : inner_padding,
          last == &item ? kInspectorSectionBodyPadding : inner_padding};
}

/// Separates native editors from quiet, consistently inset property labels.
class InspectorPropertyDelegate final : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    if (painter == nullptr || !index.isValid()) {
      return;
    }
    QStyleOptionViewItem styled(option);
    painter->save();
    if (!index.parent().isValid() && index.model()->hasChildren(index)) {
      // The retained native button owns the complete section-header surface.
      painter->fillRect(option.rect, option.palette.base());
      painter->restore();
      return;
    }
    styled.backgroundBrush = option.palette.base();
    painter->fillRect(option.rect, styled.backgroundBrush);
    if (index.column() == 0 && index.data(kInspectorRetainedLabelRole).toBool()) {
      painter->restore();
      return;
    }
    if (index.column() == 0) {
      const int inset = index.data(kInspectorPropertyInsetRole).toInt();
      if (option.direction == Qt::RightToLeft) {
        styled.rect.setRight(option.rect.right() - inset);
      } else {
        styled.rect.setLeft(option.rect.left() + inset);
      }
      for (const QPalette::ColorGroup group :
           {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        styled.palette.setColor(
            group, QPalette::Text,
            option.palette.color(group, QPalette::WindowText));
      }
      styled.rect.adjust(0, index.data(kInspectorPropertyTopPaddingRole).toInt(),
                         0, -index.data(kInspectorPropertyBottomPaddingRole).toInt());
      const int axis_row_padding =
          index.data(kInspectorAxisRowPaddingRole).toInt();
      if (axis_row_padding > 0) {
        const int axis_row_spacing =
            index.data(kInspectorAxisRowSpacingRole).toInt();
        styled.rect.setHeight((styled.rect.height() - 2 * axis_row_spacing) / 3);
      }
    }
    QStyledItemDelegate::paint(painter, styled, index);
    painter->restore();
  }
};

/// Retains the resizable label column and its interaction-only divider cue.
class InspectorPropertyTree final : public QTreeWidget {
public:
  using QTreeWidget::QTreeWidget;

  void apply_ui_scale(const QtUiScaleMetrics &metrics) { metrics_ = metrics; }

  void synchronize_column_editors() { updateEditorGeometries(); }

protected:
  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton &&
        over_column_divider(event->position().toPoint())) {
      resizing_column_ = true;
      resize_origin_ = event->position().toPoint().x();
      resize_width_ = header()->sectionSize(0);
      set_divider_hovered(true);
      event->accept();
      return;
    }
    QTreeWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (resizing_column_) {
      const int direction = layoutDirection() == Qt::RightToLeft ? -1 : 1;
      const int requested = resize_width_ + direction *
          (event->position().toPoint().x() - resize_origin_);
      const int minimum = header()->minimumSectionSize();
      const int maximum = std::max(minimum, viewport()->width() -
          metrics_.px(kInspectorMinimumValueColumnWidth));
      header()->resizeSection(0, std::clamp(requested, minimum, maximum));
      viewport()->update();
      event->accept();
      return;
    }
    set_divider_hovered(over_column_divider(event->position().toPoint()));
    QTreeWidget::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if (resizing_column_ && event->button() == Qt::LeftButton) {
      resizing_column_ = false;
      set_divider_hovered(over_column_divider(event->position().toPoint()));
      viewport()->update();
      event->accept();
      return;
    }
    QTreeWidget::mouseReleaseEvent(event);
  }

  bool viewportEvent(QEvent *event) override {
    if (!resizing_column_) {
      if (event->type() == QEvent::HoverMove) {
        // Hover moves also reach this viewport over retained child editors.
        // Its inherited cursor must stop at the divider's actual hit target.
        const auto *hover = static_cast<QHoverEvent *>(event);
        set_divider_hovered(over_column_divider(hover->position().toPoint()));
      } else if (event->type() == QEvent::Leave) {
        set_divider_hovered(false);
      }
    }
    return QTreeWidget::viewportEvent(event);
  }

  void paintEvent(QPaintEvent *event) override {
    QTreeWidget::paintEvent(event);
    if (!divider_hovered_ && !resizing_column_) {
      return;
    }
    QPainter painter(viewport());
    const qreal stroke = 1.0 / std::max<qreal>(1.0, devicePixelRatioF());
    painter.setPen(QPen(palette().color(QPalette::Accent), stroke));
    const qreal x = column_divider() - stroke * 0.5;
    for (int index = 0; index < topLevelItemCount(); ++index) {
      QTreeWidgetItem *item = topLevelItem(index);
      if (item->isHidden() || !item->isExpanded()) {
        continue;
      }
      for (int child_index = 0; child_index < item->childCount(); ++child_index) {
        QTreeWidgetItem *child = item->child(child_index);
        if (child->isHidden() || child->isFirstColumnSpanned()) {
          continue;
        }
        const QRect bounds = visualItemRect(child);
        if (bounds.intersects(viewport()->rect())) {
          painter.drawLine(QPointF(x, bounds.top() + metrics_.px(4)),
                           QPointF(x, bounds.bottom() - metrics_.px(4)));
        }
      }
    }
  }

private:
  [[nodiscard]] int column_divider() const {
    const int start = header()->sectionViewportPosition(0);
    return layoutDirection() == Qt::RightToLeft
               ? start : start + header()->sectionSize(0);
  }

  [[nodiscard]] bool over_column_divider(const QPoint &position) const {
    const auto *item = itemAt(position);
    return item != nullptr && item->parent() != nullptr &&
           !item->isFirstColumnSpanned() &&
           std::abs(position.x() - column_divider()) <= metrics_.px(4);
  }

  void set_divider_hovered(bool hovered) {
    if (divider_hovered_ == hovered) {
      return;
    }
    divider_hovered_ = hovered;
    if (hovered) {
      viewport()->setCursor(Qt::SizeHorCursor);
    } else {
      viewport()->unsetCursor();
    }
    viewport()->update();
  }

  QtUiScaleMetrics metrics_;
  bool divider_hovered_ = false;
  bool resizing_column_ = false;
  int resize_origin_ = 0;
  int resize_width_ = 0;
};

/// Identifies which transform component an inspector preview edits.
enum class TransformEditKind {
  Location,
  Rotation,
  Scale,
};

/// Captures the selection and baseline values for one transform edit preview.
struct TransformEditState {
  TransformEditKind kind = TransformEditKind::Location;
  EntityId primary = kInvalidEntityId;
  std::vector<EntityId> selection;
  std::array<float, 3> values{};
  std::array<double, 3> scale_baseline{};
  double scale_factor_minimum = 0.0;
  double scale_factor_maximum = 0.0;
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

  /// Retains authored bounds independently from legacy out-of-range values.
  struct NumberField {
    QPointer<QDoubleSpinBox> number;
    double numeric_minimum = 0.0;
    double numeric_maximum = 0.0;
  };

  /// Retains disclosure controls while their tree items own expansion state.
  struct SectionHeader {
    QTreeWidgetItem *item = nullptr;
    QtPanelSectionHeader *button = nullptr;
  };

  /// Retains layout metrics without replacing active property editors.
  struct PropertyRow {
    QTreeWidgetItem *item = nullptr;
    QVBoxLayout *layout = nullptr;
    QGridLayout *axes = nullptr;
    QGridLayout *label_axes = nullptr;
    QHBoxLayout *label_header = nullptr;
    QtScaleLinkButton *scale_link = nullptr;
  };

  /// Retains the displayed axes and the values used by their edit callbacks.
  struct TransformRow {
    std::array<QPointer<QDoubleSpinBox>, 3> fields;
    std::array<bool, 3> mixed;
    std::shared_ptr<std::array<double, 3>> values;
  };

  /// Latest absolute pointer sample within one live transform preview.
  struct PendingTransformPreview {
    TransformEditKind kind = TransformEditKind::Location;
    std::array<double, 3> values{};
    std::uint64_t binding_generation = 0;
    std::uint64_t selection_revision = 0;
    std::uint64_t editor_mode_generation = 0;
  };

  explicit Impl(InspectorPanel &panel) : panel_(panel) {
    panel_.setObjectName(
        QString::fromLatin1(ui_panel_stable_id(UIPanelId::Inspector)));
    panel_.setWindowTitle(
        QString::fromLatin1(ui_panel_key(UIPanelId::Inspector)));

    auto *root_layout = new QVBoxLayout(&panel_);
    root_layout->setObjectName(QStringLiteral("inspector.layout"));
    root_layout->setContentsMargins(0, 0, 0, 0);

    tree_ = new InspectorPropertyTree(&panel_);
    tree_->setObjectName(QStringLiteral("inspector.tree"));
    tree_->setColumnCount(2);
    tree_->setRootIsDecorated(false);
    tree_->setItemsExpandable(true);
    tree_->setAlternatingRowColors(false);
    tree_->setIndentation(0);
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->viewport()->setBackgroundRole(QPalette::Base);
    tree_->setUniformRowHeights(false);
    tree_->setWordWrap(false);
    tree_->setSelectionMode(QAbstractItemView::NoSelection);
    tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    tree_->setTextElideMode(Qt::ElideRight);
    tree_->setItemDelegate(new InspectorPropertyDelegate(tree_));
    panel_.setFocusProxy(tree_);
    tree_->header()->setVisible(false);
    tree_->setMouseTracking(true);
    tree_->viewport()->setAttribute(Qt::WA_Hover, true);
    tree_->header()->setSectionsMovable(false);
    tree_->header()->setSectionsClickable(false);
    tree_->header()->setHighlightSections(false);
    tree_->header()->setMinimumSectionSize(64);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    tree_->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->resizeSection(0, 140);
    root_layout->addWidget(tree_);

    transform_dispatch_timer_ = new QTimer(&panel_);
    transform_dispatch_timer_->setObjectName(
        QStringLiteral("inspector.transform.dispatchTimer"));
    transform_dispatch_timer_->setSingleShot(true);
    transform_dispatch_timer_->setTimerType(Qt::PreciseTimer);
    transform_dispatch_timer_->setInterval(kInspectorNumericDispatchIntervalMs);
    QObject::connect(transform_dispatch_timer_, &QTimer::timeout, &panel_,
                     [this]() { flush_transform_preview(); });

    const auto remember_section_state = [this](QTreeWidgetItem *item,
                                                bool expanded) {
      if (rebuilding_ || item == nullptr) {
        return;
      }
      const QString stable_id = item->data(0, Qt::UserRole).toString();
      if (!stable_id.isEmpty()) {
        expanded_sections_[stable_id.toUtf8().toStdString()] = expanded;
      }
      if (auto *header = dynamic_cast<QtPanelSectionHeader *>(
              tree_->itemWidget(item, 0))) {
        const QSignalBlocker blocker(header);
        header->setChecked(expanded);
        header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
      }
    };
    QObject::connect(tree_, &QTreeWidget::itemExpanded, &panel_,
                     [remember_section_state](QTreeWidgetItem *item) {
                       remember_section_state(item, true);
                     });
    QObject::connect(tree_, &QTreeWidget::itemCollapsed, &panel_,
                     [this, remember_section_state](QTreeWidgetItem *item) {
                       remember_section_state(item, false);
                       if (!rebuilding_ && item != nullptr) {
                         if (auto *header = dynamic_cast<QtPanelSectionHeader *>(
                                 tree_->itemWidget(item, 0))) {
                           focus_section_header(
                               QPersistentModelIndex(tree_->indexFromItem(item)),
                               *header);
                         }
                       }
                     });
    QObject::connect(tree_->header(), &QHeaderView::sectionResized, &panel_,
                     [this](int section, int, int size) {
                       if (section == 0 && !adjusting_label_column_width_ &&
                           !rebuilding_) {
                         preferred_label_column_width_ = size;
                       }
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
    discard_pending_transform_preview();
    delete transform_dispatch_timer_;
    transform_dispatch_timer_ = nullptr;
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

  void set_component_extension(QWidget *extension) {
    if (component_extension_ == extension) return;
    auto *layout = qobject_cast<QVBoxLayout *>(panel_.layout());
    if (component_extension_ != nullptr) {
      layout->removeWidget(component_extension_);
      component_extension_->hide();
      component_extension_->setParent(nullptr);
    }
    component_extension_ = extension;
    if (extension != nullptr) {
      extension->setParent(&panel_);
      layout->addWidget(extension);
      extension->show();
    }
  }

  void abandon_runtime() noexcept {
    ++runtime_binding_generation_;
    ++property_generation_;
    discard_pending_transform_preview();
    for (QDoubleSpinBox *spin : panel_.findChildren<QDoubleSpinBox *>()) {
      qt_install_numeric_spin_box(*spin).abandon_interaction();
    }
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
    viewport_settings_ = nullptr;
    preview_document_ = nullptr;
    preview_assets_ = nullptr;
    force_rebuild_ = true;
    if (active_color_dialog_ != nullptr) {
      active_color_dialog_->reject();
    }
    active_color_dialog_ = nullptr;
    active_mesh_color_dialog_ = false;
  }

  void refresh(UIContext &context) {
    const SceneDocument *next_document = &context.scene.document;
    const ModelingSessionService *next_modeling = &context.scene.modeling;
    const bool owner_changed =
        document_ != next_document || modeling_ != next_modeling;
    const auto mode_generation = context.intent.editor.mode().generation();
    const bool mode_changed = !owner_changed &&
        built_editor_mode_generation_ != mode_generation;
    const auto mode_revision = context.intent.editor.mode().revision();
    const auto selection_revision = next_document->selection_revision();
    const bool edit_mode =
        context.intent.editor.mode().current() == QuaderEditorMode::Edit;
    const bool targets_changed = owner_changed || mode_changed ||
        (edit_mode ? inspected_mode_revision_ != mode_revision
                   : inspected_selection_revision_ != selection_revision);

    std::vector<EntityId> next_selection;
    EntityId next_primary = kInvalidEntityId;
    if (targets_changed) {
      if (edit_mode) {
        const auto mode = context.intent.editor.mode().state();
        next_selection.reserve(mode.participants.size());
        for (const auto &participant : mode.participants) {
          next_selection.push_back(participant.id);
        }
        next_primary = mode.active_entity.id;
        if (std::find(next_selection.begin(), next_selection.end(), next_primary) ==
            next_selection.end()) {
          next_primary = kInvalidEntityId;
        }
      } else {
        next_selection = next_document->selected_entities();
        next_primary = next_document->primary_selection();
      }
      if (next_primary == kInvalidEntityId && !next_selection.empty()) {
        next_primary = next_selection.front();
      }
    }
    if (owner_changed && (document_ != nullptr || modeling_ != nullptr)) {
      // The former SceneSession may already be gone when a host rebind arrives.
      // Disarm previews without restoring through their non-owning document.
      abandon_runtime();
    }
    if (mode_changed) {
      if (active_mesh_color_dialog_ && active_color_dialog_ != nullptr) {
        active_color_dialog_->reject();
      }
      // The transition owner already settled valid drafts. Retire callbacks
      // retained by old controls before rebuilding the new mode presentation.
      // Active-object changes only advance revision; their original draft must
      // still commit through selection_changed below.
      discard_pending_transform_preview();
      for (QDoubleSpinBox *spin : panel_.findChildren<QDoubleSpinBox *>()) {
        qt_install_numeric_spin_box(*spin).abandon_interaction();
      }
      active_transform_preview_.abandon();
      active_transform_edit_.reset();
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
      force_rebuild_ = true;
    }
    const bool selection_changed = targets_changed &&
        (owner_changed || next_selection != selection_ ||
         next_primary != primary_);
    if (!owner_changed && !mode_changed && selection_changed &&
        active_color_dialog_ != nullptr) {
      active_color_dialog_->accept();
    }
    if (!owner_changed && selection_changed) {
      discard_pending_transform_preview();
      if (!commit_active_transform_edit()) {
        return;
      }
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
    }
    if (selection_changed) {
      selection_ = std::move(next_selection);
      primary_ = next_primary;
      force_rebuild_ = true;
    }
    inspected_mode_revision_ = mode_revision;
    inspected_selection_revision_ = selection_revision;

    editor_ = &context.intent.editor;
    document_ = next_document;
    modeling_ = next_modeling;
    localization_ = &context.localization;
    viewport_settings_ = &context.preferences.viewport;
    const auto global_wireframe_color =
        viewport_settings_->color(ViewportColorSettingId::WireframeOnShaded);
    if (global_wireframe_color.r != presented_wireframe_color_.r ||
        global_wireframe_color.g != presented_wireframe_color_.g ||
        global_wireframe_color.b != presented_wireframe_color_.b) {
      presented_wireframe_color_ = global_wireframe_color;
      force_rebuild_ = true;
    }
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

    if (!layout_changed &&
        (refresh_transform_properties() || refresh_light_properties())) {
      return;
    }

    if (!selection_changed && active_color_dialog_ != nullptr) {
      return;
    }

    rebuild(summary);
  }

  void refresh_transform_preview(UIContext &context) {
    const SceneDocument &document = context.scene.document;
    if (document_ != &document || modeling_ != &context.scene.modeling ||
        built_editor_mode_revision_ != context.intent.editor.mode().revision() ||
        built_scene_selection_revision_ != document.selection_revision() ||
        built_modeling_selection_revision_ !=
            context.scene.modeling.selection_revision() || force_rebuild_) {
      refresh(context);
      return;
    }
    // Check the owner before selection walks, formatting, or retained setters.
    if (presented_transform_revision_ == document.transform_revision() ||
        active_transform_edit_.has_value() || continuous_edit_active_) {
      return;
    }
    if (!synchronize_transform_rows()) {
      // A Mixed/numeric transition changes the field type, so rebuild once.
      refresh(context);
    }
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    if (ui_scale_metrics_.revision() == metrics.revision() &&
        qFuzzyCompare(static_cast<qreal>(ui_scale_metrics_.factor()),
                      static_cast<qreal>(metrics.factor()))) {
      return;
    }

    QHeaderView *header = tree_->header();
    ui_scale_metrics_ = metrics;
    adjusting_label_column_width_ = true;
    header->setMinimumSectionSize(metrics.px(64));
    adjusting_label_column_width_ = false;
    update_property_layout_metrics();
    if (!property_layout_refresh_pending_) {
      property_layout_refresh_pending_ = true;
      // Qt propagates the application font and polishes retained fields after
      // scale listeners. Measure their final font once that work has settled.
      QTimer::singleShot(0, &panel_, [this]() {
        property_layout_refresh_pending_ = false;
        update_property_layout_metrics();
        tree_->doItemsLayout();
      });
    }

    const QSize preview_size = metrics.size(
        QSize(kInspectorMaterialPreviewSize, kInspectorMaterialPreviewSize));
    const QSize action_size = metrics.size(
        QSize(kInspectorMaterialActionSize, kInspectorMaterialActionSize));
    const QSize action_icon_size = metrics.size(QSize(
        kInspectorMaterialActionIconSize, kInspectorMaterialActionIconSize));
    for (MaterialRow &row : material_rows_) {
      const auto *section = row.item->parent();
      const int top_padding = section->child(0) == row.item ? 8 : 4;
      const int bottom_padding =
          section->child(section->childCount() - 1) == row.item ? 8 : 4;
      row.item->setSizeHint(0, QSize(0, metrics.px(
          kInspectorMaterialPreviewSize + top_padding + bottom_padding)));
      row.layout->setContentsMargins(metrics.margins(
          QMargins(8, top_padding, 8, bottom_padding)));
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

  bool settle_pending_edits() {
    const auto generation = runtime_binding_generation_;
    const QPointer<InspectorPanel> guard(&panel_);
    QWidget *focus = QApplication::focusWidget();
    if (focus != nullptr && panel_.isAncestorOf(focus)) {
      auto *spin = qobject_cast<QDoubleSpinBox *>(focus);
      if (spin == nullptr) {
        spin = qobject_cast<QDoubleSpinBox *>(focus->parentWidget());
      }
      if (spin != nullptr) {
        if (!spin->hasAcceptableInput()) {
          return false;
        }
        spin->interpretText();
      }
    }
    flush_transform_preview();
    if (guard == nullptr || generation != runtime_binding_generation_ ||
        !commit_active_transform_edit()) {
      return false;
    }
    if (guard == nullptr || generation != runtime_binding_generation_) {
      return false;
    }
    discard_pending_transform_preview();
    for (QDoubleSpinBox *spin : panel_.findChildren<QDoubleSpinBox *>()) {
      qt_install_numeric_spin_box(*spin).abandon_interaction();
    }
    active_continuous_edit_.reset();
    continuous_edit_active_ = false;
    active_continuous_id_.clear();
    return true;
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
    int minimum_value_width = scaled_px(kInspectorMinimumValueColumnWidth);
    for (const PropertyRow &row : property_rows_) {
      if (row.axes == nullptr) {
        continue;
      }
      // Compact numeric controls retain a readable native minimum even when
      // their container ignores its size hint. Reserve the axis gutter and
      // property padding before giving remaining width to localized labels.
      const QMargins margins = row.layout->contentsMargins();
      minimum_value_width = std::max(
          minimum_value_width, row.axes->minimumSize().width() +
                                   margins.left() + margins.right());
    }
    const int maximum_label_width =
        std::max(header->minimumSectionSize(),
                 tree_->viewport()->width() - minimum_value_width);
    const int desired_width = preferred_label_column_width_ > 0
                                  ? preferred_label_column_width_
                                  : scaled_default_label_column_width_;
    const int width = std::clamp(desired_width, header->minimumSectionSize(),
                                 maximum_label_width);
    if (header->sectionSize(0) == width) {
      return;
    }
    adjusting_label_column_width_ = true;
    header->resizeSection(0, width);
    // QTreeView defers this synchronization to a timer after sectionResized.
    // Our scale/layout correction must place retained editors before painting.
    tree_->synchronize_column_editors();
    adjusting_label_column_width_ = false;
  }

  void update_property_layout_metrics() {
    panel_.layout()->setContentsMargins(ui_scale_metrics_.margins(
        QMargins(8, 8, 8, 8)));
    tree_->apply_ui_scale(ui_scale_metrics_);
    scaled_default_label_column_width_ = scaled_px(140);
    for (PropertyRow &row : property_rows_) {
      const int vertical_padding = scaled_px(row.axes != nullptr ? 5 : 4);
      const auto *section = row.item->parent();
      const int top_padding = section->child(0) == row.item
                                  ? scaled_px(8) : vertical_padding;
      const int bottom_padding =
          section->child(section->childCount() - 1) == row.item
              ? scaled_px(8) : vertical_padding;
      row.layout->setContentsMargins(scaled_px(8), top_padding,
                                     scaled_px(8), bottom_padding);
      row.item->setData(0, kInspectorPropertyTopPaddingRole, top_padding);
      row.item->setData(0, kInspectorPropertyBottomPaddingRole, bottom_padding);
      scaled_default_label_column_width_ = std::max(
          scaled_default_label_column_width_,
          tree_->fontMetrics().horizontalAdvance(row.item->text(0)) +
              scaled_px(28));
      row.item->setData(0, kInspectorPropertyInsetRole, scaled_px(16));
      if (row.axes != nullptr) {
        row.axes->setHorizontalSpacing(scaled_px(6));
        row.axes->setVerticalSpacing(scaled_px(3));
        row.item->setData(0, kInspectorAxisRowPaddingRole, vertical_padding);
        row.item->setData(0, kInspectorAxisRowSpacingRole, scaled_px(3));
      }
      if (row.scale_link != nullptr) {
        row.scale_link->apply_ui_scale(ui_scale_metrics_);
        row.label_axes->setContentsMargins(scaled_px(16), top_padding,
                                          scaled_px(8), bottom_padding);
        row.label_axes->setVerticalSpacing(scaled_px(3));
        row.label_header->setSpacing(scaled_px(4));
        scaled_default_label_column_width_ = std::max(
            scaled_default_label_column_width_,
            tree_->fontMetrics().horizontalAdvance(row.item->text(0)) +
                scaled_px(28) + row.scale_link->sizeHint().width());
      }
    }
    for (MaterialRow &row : material_rows_) {
      const auto *section = row.item->parent();
      const int top_padding = section->child(0) == row.item ? 8 : 4;
      const int bottom_padding =
          section->child(section->childCount() - 1) == row.item ? 8 : 4;
      row.layout->setContentsMargins(ui_scale_metrics_.margins(
          QMargins(8, top_padding, 8, bottom_padding)));
      row.item->setSizeHint(0, QSize(0, scaled_px(
          kInspectorMaterialPreviewSize + top_padding + bottom_padding)));
    }
    for (const SectionHeader &header : section_headers_) {
      header.button->apply_ui_scale(ui_scale_metrics_);
      header.item->setSizeHint(0, QSize(0, header.button->minimumHeight()));
    }
    clamp_label_column_width();
  }

  QString localized(std::string_view key, std::string_view fallback) const {
    return localization_ != nullptr
               ? qt_text(localization_->text(key, fallback))
               : qt_text(fallback);
  }

  QString property_help(std::string_view stable_id) const {
    const auto help = qt_inspector_property_help(stable_id);
    return help.key.empty() ? QString{} : localized(help.key, help.fallback);
  }

  QString stable_name(std::string_view stable_id) const {
    QString result = QStringLiteral("inspector.");
    result.append(qt_text(stable_id));
    return result;
  }

  void clear_content() {
    ++property_generation_;
    discard_pending_transform_preview();
    section_headers_.clear();
    property_rows_.clear();
    material_rows_.clear();
    transform_rows_.clear();
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

  bool synchronize_transform_rows() {
    if (transform_rows_.empty()) {
      presented_transform_revision_ = document_->transform_revision();
      return true;
    }
    const TransformComponent *transform = document_->transform(primary_);
    if (transform == nullptr) {
      return false;
    }
    const std::array<std::array<bool, 3>, 3> mixed{
        mixed_location_axes(*document_, selection_, *transform),
        mixed_rotation_axes(*document_, selection_, *transform),
        mixed_scale_axes(*document_, selection_, *transform),
    };
    for (std::size_t index = 0; index < transform_rows_.size(); ++index) {
      if (transform_rows_[index].mixed != mixed[index]) {
        return false;
      }
    }
    const QVec3 rotation = positive_euler_radians(transform->rotation_euler);
    const std::array<std::array<double, 3>, 3> values{{
        {transform->location.x, transform->location.y, transform->location.z},
        {rotation.x * kRadToDeg, rotation.y * kRadToDeg,
         rotation.z * kRadToDeg},
        {transform->scale.x, transform->scale.y, transform->scale.z},
    }};
    for (std::size_t index = 0; index < transform_rows_.size(); ++index) {
      TransformRow &row = transform_rows_[index];
      for (std::size_t axis = 0; axis < 3; ++axis) {
        if ((*row.values)[axis] == values[index][axis]) {
          continue;
        }
        (*row.values)[axis] = values[index][axis];
        if (row.fields[axis] != nullptr) {
          const QSignalBlocker blocker(row.fields[axis].data());
          row.fields[axis]->setValue(values[index][axis]);
        }
      }
    }
    presented_transform_revision_ = document_->transform_revision();
    return true;
  }

  bool refresh_transform_properties() {
    const std::uint64_t scene_revision = document_->revision();
    const std::uint64_t transform_revision = document_->transform_revision();
    if (scene_revision <= built_scene_revision_ ||
        transform_revision <= built_transform_revision_ ||
        scene_revision - built_scene_revision_ !=
            transform_revision - built_transform_revision_ ||
        !synchronize_transform_rows()) {
      return false;
    }
    built_scene_revision_ = scene_revision;
    built_transform_revision_ = transform_revision;
    return true;
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

    if (selection_.empty()) {
      build_empty_selection(summary);
    } else {
      build_general_section();
      build_visibility_section();
      build_transform_section();
      if (editor_->mode().current() == QuaderEditorMode::Object ||
          summary.mode == ModelingSelectionMode::Face) {
        build_material_section();
      }

      if (selection_.size() == 1u) {
        build_mesh_sections();
        build_light_section();
      }
      build_viewport_section();
      build_physics_section();
      if (selection_.size() > 1u) {
        build_component_count_section();
      }
    }

    built_scene_revision_ = document_->revision();
    built_light_property_revision_ = document_->light_property_revision();
    built_transform_revision_ = document_->transform_revision();
    presented_transform_revision_ = built_transform_revision_;
    built_scene_selection_revision_ = document_->selection_revision();
    built_modeling_content_revision_ = summary.content_revision;
    built_modeling_selection_revision_ = summary.selection_revision;
    built_editor_mode_revision_ = editor_->mode().revision();
    built_editor_mode_generation_ = editor_->mode().generation();
    built_material_presentation_revision_ = material_presentation_.revision;
    built_locale_ = localization_->current_locale();
    force_rebuild_ = false;
    update_property_layout_metrics();
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
    item->setToolTip(0, property_help(stable_id));
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

  bool focus_section_header(const QPersistentModelIndex &index,
                              QtPanelSectionHeader &header) {
    const auto generation = runtime_binding_generation_;
    const QPointer<InspectorPanel> panel_guard(&panel_);
    const QPointer<QtPanelSectionHeader> header_guard(&header);
    const auto current = [&]() {
      return panel_guard && header_guard && index.isValid() &&
             generation == runtime_binding_generation_;
    };
    if (QWidget *focus = QApplication::focusWidget();
        focus != nullptr && focus != &header && panel_.isAncestorOf(focus)) {
      focus->clearFocus();
    }
    if (!current()) {
      return false;
    }
    // A hidden current editor can make later synchronization reopen ancestors.
    tree_->setCurrentItem(tree_->topLevelItem(index.row()));
    if (!current()) {
      return false;
    }
    header.setFocus(Qt::OtherFocusReason);
    return current();
  }

  Section add_section(std::string stable_id, const QString &title,
                      bool default_expanded = true) {
    const auto state =
        expanded_sections_.try_emplace(stable_id, default_expanded).first;
    auto *item = new QTreeWidgetItem(tree_);
    item->setText(0, title);
    item->setData(0, Qt::UserRole, qt_text(stable_id));
    item->setFlags(Qt::ItemIsEnabled);
    item->setFirstColumnSpanned(true);
    item->setExpanded(state->second);
    auto *header = new QtPanelSectionHeader(tree_->viewport());
    header->setObjectName(stable_name(stable_id + ".header"));
    header->setText(title);
    header->setAccessibleName(title);
    header->setToolTip(property_help(stable_id));
    header->setChecked(state->second);
    header->setArrowType(state->second ? Qt::DownArrow : Qt::RightArrow);
    header->set_leading_spacing(section_headers_.empty() ? 0 : 8);
    tree_->setItemWidget(item, 0, header);
    section_headers_.push_back({item, header});
    const QPersistentModelIndex index(tree_->indexFromItem(item));
    QObject::connect(header, &QToolButton::toggled, header,
                     [this, index, header](bool expanded) {
      if (rebuilding_ || !index.isValid()) {
        return;
      }
      if (!focus_section_header(index, *header)) {
        return;
      }
      tree_->topLevelItem(index.row())->setExpanded(expanded);
    });
    return {.item = item, .editor_parent = tree_->viewport()};
  }

  void add_row(Section &section, std::string_view stable_id,
               const QString &label, QWidget *field,
               QGridLayout *axes = nullptr) {
    auto *item = new QTreeWidgetItem(section.item);
    item->setText(0, label);
    item->setToolTip(0, property_help(stable_id));
    item->setData(0, Qt::UserRole, stable_name(stable_id));
    item->setFlags(Qt::ItemIsEnabled);
    item->setTextAlignment(0, Qt::AlignLeft | Qt::AlignVCenter);

    auto *container = new QWidget(section.editor_parent);
    container->setObjectName(stable_name(stable_id) + QStringLiteral(".container"));
    container->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(container);
    layout->setSpacing(0);
    const int vertical_padding = scaled_px(axes != nullptr ? 5 : 4);
    layout->setContentsMargins(scaled_px(8), vertical_padding,
                               scaled_px(8), vertical_padding);
    field->setMinimumWidth(0);
    QSizePolicy policy = field->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    policy.setHorizontalStretch(1);
    field->setSizePolicy(policy);
    layout->addWidget(field);
    if (field->accessibleName().isEmpty()) {
      field->setAccessibleName(label);
    }
    property_rows_.push_back({item, layout, axes});
    tree_->setItemWidget(item, 1, container);
  }

  void build_material_section() {
    if (!release_has_feature(ReleaseFeature::Materials) ||
        !material_presentation_.section_visible) {
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
      layout->setContentsMargins(scaled_px(8), scaled_px(4), scaled_px(8),
                                 scaled_px(4));
      layout->setSpacing(scaled_px(6));

      auto *preview = new QLabel(row);
      preview->setObjectName(stable_name(stable_id + ".preview"));
      preview->setFixedSize(material_preview_size, material_preview_size);
      preview->setAlignment(Qt::AlignCenter);
      preview->setToolTip(property_help(stable_id));
      preview->setFrameShape(QFrame::StyledPanel);
      layout->addWidget(preview);

      const QString display_name =
          material.name.isEmpty()
              ? localized("ui.inspector.material.missing", "Missing material")
              : material.name;
      auto *name = new InspectorElidedLabel(display_name, property_help(stable_id), row);
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
    const auto property_generation = property_generation_;
    QObject::connect(box, &QCheckBox::toggled, &panel_,
                     [this, property_generation,
                      changed = std::move(changed)](bool value) {
                       if (!rebuilding_ &&
                           property_context_current(property_generation)) {
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
    const auto property_generation = property_generation_;
    QObject::connect(box, &QCheckBox::toggled, &panel_,
                     [this, property_generation,
                      changed = std::move(changed)](bool value) {
                       if (!rebuilding_ &&
                           property_context_current(property_generation)) {
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
    const auto property_generation = property_generation_;
    QObject::connect(combo, &QComboBox::activated, &panel_,
                     [this, combo, property_generation,
                      changed = std::move(changed)](int index) {
                       if (rebuilding_ ||
                           !property_context_current(property_generation) || index < 0 ||
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

    const auto property_generation = property_generation_;
    QObject::connect(
        spin, &QDoubleSpinBox::valueChanged, &panel_,
        [this, stable_id, property_generation,
         changed = std::move(changed)](double next) {
          if (rebuilding_ || !property_context_current(property_generation)) {
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
    const auto finish = [this, stable_id, property_generation]() {
      if (rebuilding_ || !property_context_current(property_generation) ||
          active_continuous_id_ != stable_id) {
        return;
      }
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
      force_rebuild_ = true;
    };
    QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_, finish);
    qt_install_numeric_spin_box(*spin).interaction_canceled = finish;
    return spin;
  }

  void synchronize_number_field(const NumberField &field, double value) {
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
  }

  NumberField add_number_field(
      Section &section, const std::string &stable_id, const QString &label,
      double value, double minimum, double numeric_maximum, double step,
      std::function<void(double, std::string)> changed) {
    NumberField field;
    field.numeric_minimum = minimum;
    field.numeric_maximum = numeric_maximum;
    auto *spin = new InspectorDoubleSpinBox(section.editor_parent);
    field.number = spin;
    spin->setObjectName(stable_name(stable_id));
    // Retain conversion precision; QtDoubleSpinBox formats at most four decimals.
    spin->setDecimals(12);
    spin->setSingleStep(step);
    spin->setKeyboardTracking(true);
    spin->setMinimumWidth(0);
    spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    synchronize_number_field(field, value);
    add_row(section, stable_id, label, spin);

    const auto property_generation = property_generation_;
    const auto update = [this, stable_id, field, property_generation,
                         changed = std::move(changed)](double requested) {
      if (rebuilding_ || !property_context_current(property_generation)) {
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
    const auto finish = [this, stable_id, property_generation]() {
      if (rebuilding_ || !property_context_current(property_generation) ||
          active_continuous_id_ != stable_id) {
        return;
      }
      active_continuous_edit_.reset();
      continuous_edit_active_ = false;
      active_continuous_id_.clear();
    };
    QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_, finish);
    qt_install_numeric_spin_box(*spin).interaction_canceled = finish;
    return field;
  }

  void synchronize_light_controls(const LightComponent &light) {
    synchronize_number_field(light_intensity_field_, light.intensity);
    synchronize_number_field(light_falloff_field_, light.distance_attenuation);
    synchronize_number_field(light_temperature_field_,
                               light.color_temperature_kelvin);
    const bool temperature_mode =
        light.color_mode == LightColorMode::ColorTemperature;
    if (light_temperature_field_.number != nullptr &&
        light_temperature_field_.number->isEnabled() != temperature_mode) {
      light_temperature_field_.number->setEnabled(temperature_mode);
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
                    EntityId entity) {
    const QColor initial = QColor::fromRgbF(std::clamp(value.x, 0.0f, 1.0f),
                                            std::clamp(value.y, 0.0f, 1.0f),
                                            std::clamp(value.z, 0.0f, 1.0f));
    auto *button = new QtColorSwatchButton(section.editor_parent);
    button->setObjectName(stable_name(stable_id));
    button->setAccessibleName(label);
    button->set_swatch_color(initial);
    add_row(section, stable_id, label, button);

    const auto property_generation = property_generation_;
    QObject::connect(
        button, &QtColorSwatchButton::clicked, &panel_,
        [this, stable_id, button, entity, property_generation]() {
          if (!property_context_current(property_generation)) {
            return;
          }
          const std::uint64_t dialog_generation = runtime_binding_generation_;
          QPointer<InspectorPanel> panel_guard(&panel_);
          const auto edit = editor_->scene().begin_light_color_edit(entity);
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
               session = edit.value()](const QColor &color) {
                if (panel_guard == nullptr || !color.isValid() ||
                    runtime_binding_generation_ != dialog_generation ||
                    editor_ == nullptr) {
                  return;
                }
                QVec3 next{static_cast<float>(color.redF()),
                           static_cast<float>(color.greenF()),
                           static_cast<float>(color.blueF())};
                next = light_srgb_to_linear(next);
                const auto result = editor_->scene().update_light_color_edit(
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
                if (document_ != nullptr) {
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
          const auto finished = editor_->scene().finish_light_color_edit(
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
      const std::function<void(MeshComponent &)> &mutation,
      std::string history_label = "Set Mesh Physics") {
    if (editor_ == nullptr || document_ == nullptr) {
      return;
    }
    if (entities.size() == 1u) {
      apply_mesh_component(entities.front(), mutation);
      return;
    }
    auto batch = editor_->scene().batch(std::move(history_label));
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
    if (selection_.size() != 1u) {
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
      add_row(section, "general.name", localized("ui.inspector.name", "Name"),
              name);
      const EntityId entity = primary_;
      const auto property_generation = property_generation_;
      QObject::connect(name, &QLineEdit::editingFinished, &panel_,
                       [this, name, entity, property_generation]() {
                         if (!rebuilding_ &&
                             property_context_current(property_generation)) {
                           (void)editor_->scene().rename(
                               entity, name->text().toUtf8().toStdString());
                         }
                       });
    }
  }

  void build_visibility_section() {
    const std::vector<EntityId> &editable_selected = selection_;
    const BoolAggregate visibility =
        aggregate_visibility(*document_, editable_selected);
    const BoolAggregate selectability =
        aggregate_selectability(*document_, editable_selected);
    if (!visibility.has_value && !selectability.has_value) {
      return;
    }
    Section section = add_section(
        "visibility", localized("ui.inspector.visibility", "Visibility"));

    if (visibility.has_value) {
      add_mixed_checkbox(section, "visibility.visible",
                         localized("ui.inspector.visible", "Visible"),
                         visibility.all_true,
                         visibility.mixed(),
                         [this, editable_selected](bool next_visible) {
                           std::vector<QuaderSceneVisibilityEdit> edits;
                           edits.reserve(editable_selected.size());
                           for (const EntityId entity : editable_selected) {
                             if (const VisibilityComponent *current =
                                     document_->visibility(entity)) {
                               edits.push_back({.entity = entity,
                                                .before = current->visible,
                                                .after = next_visible});
                             }
                           }
                           (void)editor_->scene().set_visibility(
                               std::span<const QuaderSceneVisibilityEdit>(
                                   edits.data(), edits.size()));
                         });
    }

    if (selectability.has_value) {
      add_mixed_checkbox(
          section, "visibility.selectable",
          localized("ui.inspector.selectable", "Selectable"),
          selectability.all_true, selectability.mixed(),
          [this, editable_selected](bool next_selectable) {
            std::vector<QuaderSceneSelectabilityEdit> edits;
            edits.reserve(editable_selected.size());
            for (const EntityId entity : editable_selected) {
              if (const SelectableComponent *current = document_->selectable(entity)) {
                edits.push_back({.entity = entity,
                                 .before = current->selectable,
                                 .after = next_selectable});
              }
            }
            (void)editor_->scene().set_selectability(edits);
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

    const std::array<double, 3> location{
        transform->location.x,
        transform->location.y,
        transform->location.z,
    };
    add_transform_row(section, "transform.location",
                      localized("ui.inspector.location", "Location"),
                      TransformEditKind::Location, location,
                      mixed_location_axes(*document_, selection_, *transform),
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
    TransformRow row{.mixed = mixed, .values = values};
    auto *field = new QWidget(section.editor_parent);
    field->setObjectName(stable_name(stable_id));
    field->setMinimumWidth(0);
    field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *layout = new QGridLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(scaled_px(6));
    layout->setVerticalSpacing(scaled_px(3));
    layout->setColumnStretch(1, 1);
    constexpr std::array<const char *, 3> kAxes{{"x", "y", "z"}};
    constexpr std::array<const char *, 3> kAxisLabels{{"X", "Y", "Z"}};
    constexpr std::array<quader_editor_core::ViewAxis, 3> kAxisKinds{{
        quader_editor_core::ViewAxis::X,
        quader_editor_core::ViewAxis::Y,
        quader_editor_core::ViewAxis::Z,
    }};

    for (std::size_t axis = 0; axis < kAxes.size(); ++axis) {
      const int axis_row = static_cast<int>(axis);
      auto *axis_label =
          new QLabel(QString::fromLatin1(kAxisLabels[axis]), field);
      axis_label->setObjectName(
          stable_name(stable_id + "." + kAxes[axis] + ".label"));
      axis_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      axis_label->setToolTip(property_help(stable_id + "." + kAxes[axis]));
      apply_qt_semantic_axis_label_color(*axis_label, kAxisKinds[axis]);
      layout->addWidget(axis_label, axis_row, 0);

      if (mixed[axis]) {
        auto *mixed_field =
            new QLineEdit(localized("ui.inspector.mixed", "Mixed"), field);
        mixed_field->setObjectName(stable_name(stable_id + "." + kAxes[axis]));
        mixed_field->setReadOnly(true);
        mixed_field->setEnabled(false);
        mixed_field->setAccessibleName(label + QStringLiteral(" ") +
                                      axis_label->text());
        mixed_field->setMinimumWidth(0);
        mixed_field->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        axis_label->setBuddy(mixed_field);
        layout->addWidget(mixed_field, axis_row, 1);
        continue;
      }

      auto *spin = new InspectorDoubleSpinBox(field);
      row.fields[axis] = spin;
      spin->setObjectName(stable_name(stable_id + "." + kAxes[axis]));
      spin->setAccessibleName(label + QStringLiteral(" ") + axis_label->text());
      axis_label->setBuddy(spin);
      spin->setRange(minimum, maximum);
      spin->setSingleStep(step);
      spin->setDecimals(3);
      auto &numeric = qt_install_numeric_spin_box(*spin);
      if (kind != TransformEditKind::Rotation) {
        numeric.set_drag_sensitivity(0.01);
      }
      spin->setKeyboardTracking(true);
      spin->setValue((*values)[axis]);
      spin->setMinimumWidth(0);
      spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
      layout->addWidget(spin, axis_row, 1);

      const auto property_generation = property_generation_;
      QObject::connect(spin, &QDoubleSpinBox::valueChanged, &panel_,
                       [this, kind, axis, values, property_generation,
                        controller = &numeric](double next) {
                         if (rebuilding_ ||
                             !property_context_current(property_generation)) {
                           return;
                         }
                         (*values)[axis] = next;
                         if (controller->is_canceling()) {
                           discard_pending_transform_preview();
                           return;
                         }
                         if (kind == TransformEditKind::Scale && scale_linked_) {
                           bool constrained = false;
                           if (!prepare_linked_scale(axis, next, *values,
                                                      constrained)) {
                             force_rebuild_ = true;
                             return;
                           }
                           if (constrained) {
                             controller->synchronize_drag_value();
                           }
                         }
                         if (controller->is_dragging()) {
                           queue_transform_preview(kind, *values);
                         } else {
                           preview_transform_edit(kind, *values);
                         }
                       });
      QObject::connect(spin, &QDoubleSpinBox::editingFinished, &panel_,
                       [this, kind, property_generation]() {
                         if (!rebuilding_ &&
                             property_context_current(property_generation)) {
                           finish_transform_edit(kind);
                         }
                       });
      spin->cancel_handler = [this, kind, property_generation]() {
        if (property_context_current(property_generation) &&
            active_transform_edit_ && active_transform_edit_->kind == kind) {
          cancel_active_transform_edit();
          force_rebuild_ = true;
        }
      };
      qt_install_numeric_spin_box(*spin).interaction_canceled = [spin]() {
        const QPointer<InspectorDoubleSpinBox> guard(spin);
        const auto cancel = spin->cancel_handler;
        if (cancel) {
          cancel();
        }
        if (guard != nullptr) {
          const QSignalBlocker blocker(spin);
          spin->clearFocus();
        }
      };
    }
    transform_rows_.push_back(std::move(row));
    add_row(section, stable_id, label, field, layout);
    if (kind == TransformEditKind::Scale) {
      add_scale_link_label(property_rows_.back(), label, stable_id);
    }
  }

  void add_scale_link_label(PropertyRow &row, const QString &label,
                            std::string_view stable_id) {
    auto *container = new QWidget(tree_->viewport());
    container->setObjectName(stable_name(stable_id) + QStringLiteral(".labelRow"));
    container->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto *axes = new QGridLayout(container);
    axes->setHorizontalSpacing(0);
    auto *header = new QHBoxLayout;
    auto *text = new InspectorElidedLabel(label, property_help(stable_id),
                                         container, true);
    text->setObjectName(stable_name(stable_id) + QStringLiteral(".label"));
    text->setMinimumWidth(0);
    text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    header->addWidget(text, 1);
    auto *button = new QtScaleLinkButton(container);
    button->setObjectName(stable_name(stable_id) + QStringLiteral(".link"));
    button->setAccessibleName(localized("ui.scale_link.title", "Link Scale"));
    button->setToolTip(localized(
        "ui.scale_link.tooltip", "Preserve proportions when editing any scale value."));
    button->setChecked(scale_linked_);
    header->addWidget(button, 0, Qt::AlignVCenter);
    axes->addLayout(header, 0, 0);
    for (int axis = 0; axis < 3; ++axis) {
      axes->setRowStretch(axis, 1);
    }
    row.label_axes = axes;
    row.label_header = header;
    row.scale_link = button;
    row.item->setData(0, kInspectorRetainedLabelRole, true);
    tree_->setItemWidget(row.item, 0, container);
    const auto property_generation = property_generation_;
    QObject::connect(button, &QToolButton::pressed, &panel_,
                     [this, property_generation,
                      guard = QPointer<QtScaleLinkButton>(button)]() {
      if (!property_context_current(property_generation)) {
        return;
      }
      const QPointer<InspectorPanel> panel_guard(&panel_);
      if (QWidget *focus = QApplication::focusWidget();
          focus != nullptr && focus != guard && panel_.isAncestorOf(focus)) {
        focus->clearFocus();
      }
      if (panel_guard == nullptr || guard == nullptr ||
          !property_context_current(property_generation)) {
        return;
      }
      if (active_transform_edit_) {
        finish_transform_edit(active_transform_edit_->kind);
      }
    });
    QObject::connect(button, &QToolButton::toggled, &panel_,
                     [this, property_generation](bool checked) {
      if (property_context_current(property_generation)) {
        scale_linked_ = checked;
      }
    });
  }

  bool prepare_linked_scale(std::size_t axis, double next,
                            std::array<double, 3> &values, bool &constrained) {
    if (!begin_transform_edit(TransformEditKind::Scale, values)) {
      return false;
    }
    const auto &edit = *active_transform_edit_;
    if (!std::isfinite(next) || edit.scale_factor_minimum <= 0.0 ||
        edit.scale_factor_maximum < edit.scale_factor_minimum) {
      cancel_active_transform_edit();
      return false;
    }
    const double requested_factor = next / edit.scale_baseline[axis];
    const double factor = std::clamp(requested_factor,
                                     edit.scale_factor_minimum,
                                     edit.scale_factor_maximum);
    constrained = factor != requested_factor;
    TransformRow &row = transform_rows_[2];
    for (std::size_t component = 0; component < values.size(); ++component) {
      values[component] = edit.scale_baseline[component] * factor;
      if (row.fields[component] != nullptr) {
        const QSignalBlocker blocker(row.fields[component].data());
        row.fields[component]->setValue(values[component]);
      }
    }
    return true;
  }

  bool transform_edit_matches(TransformEditKind kind) const {
    return active_transform_edit_ && active_transform_edit_->kind == kind &&
           active_transform_edit_->primary == primary_ &&
           active_transform_edit_->selection == selection_;
  }

  bool begin_transform_edit(TransformEditKind kind,
                            const std::array<double, 3> &values) {
    if (!property_context_current() || document_ == nullptr ||
        document_->selection_revision() != built_scene_selection_revision_) {
      return false;
    }
    if (transform_edit_matches(kind) &&
        active_transform_preview_.preview_active()) {
      return true;
    }
    if (preview_document_ == nullptr || preview_assets_ == nullptr ||
        editor_ == nullptr) {
      return false;
    }

    if (active_transform_edit_) {
      const std::uint64_t generation = runtime_binding_generation_;
      const QPointer<InspectorPanel> guard(&panel_);
      const TransformEditKind previous_kind = active_transform_edit_->kind;
      finish_transform_edit(previous_kind);
      if (guard == nullptr || generation != runtime_binding_generation_ ||
          active_transform_edit_) {
        return false;
      }
    }
    const TextureLockSettings texture_locks{
        .texture_lock = editor_->preferences().texture_lock_enabled(),
        .texture_lock_scale =
            editor_->preferences().texture_lock_scale_enabled(),
    };
    SceneTextureTransformKind transform_kind = SceneTextureTransformKind::Translation;
    switch (kind) {
    case TransformEditKind::Location:
      break;
    case TransformEditKind::Rotation:
      transform_kind = SceneTextureTransformKind::Rotation;
      break;
    case TransformEditKind::Scale:
      transform_kind = SceneTextureTransformKind::Scale;
      break;
    }
    const bool began = active_transform_preview_.begin_inspector_transform_preview(
        *editor_, *preview_document_, *preview_assets_, primary_, transform_kind,
        "Set Transform", texture_locks);
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
    if (kind == TransformEditKind::Scale && scale_linked_) {
      auto &edit = *active_transform_edit_;
      const TransformComponent *primary_transform = document_->transform(primary_);
      if (primary_transform == nullptr) {
        cancel_active_transform_edit();
        return false;
      }
      edit.scale_baseline = {primary_transform->scale.x,
                             primary_transform->scale.y,
                             primary_transform->scale.z};
      edit.scale_factor_minimum = std::numeric_limits<double>::min();
      edit.scale_factor_maximum = std::numeric_limits<double>::max();
      const auto constrain = [&](QVec3 baseline) {
        for (const double component : {baseline.x, baseline.y, baseline.z}) {
          // The existing numeric transform owner clamps non-positive scales;
          // refuse those baselines instead of silently changing their ratios.
          if (!std::isfinite(component) || component <= 0.0) {
            return false;
          }
          edit.scale_factor_minimum = std::max(
              edit.scale_factor_minimum, 0.001 / component);
          edit.scale_factor_maximum = std::min(
              edit.scale_factor_maximum, 1000.0 / component);
        }
        return edit.scale_factor_minimum <= edit.scale_factor_maximum;
      };
      // The primary ratio denominator uses a fallback below this threshold.
      // Such a channel cannot represent an exact proportional numeric edit.
      if (std::any_of(edit.scale_baseline.begin(), edit.scale_baseline.end(),
                      [](double value) { return value <= 0.000001F; }) ||
          !constrain(primary_transform->scale)) {
        cancel_active_transform_edit();
        return false;
      }
      for (const auto &target : active_transform_preview_.targets()) {
        if (!constrain(target.transform.scale)) {
          cancel_active_transform_edit();
          return false;
        }
      }
    }
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
    case TransformEditKind::Location:
      result = active_transform_preview_.preview_selected_to_primary_location(
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

  /// Capture the original owner state once, then replace unpublished samples.
  void queue_transform_preview(TransformEditKind kind,
                               const std::array<double, 3> &values) {
    if (!begin_transform_edit(kind, values)) {
      force_rebuild_ = true;
      return;
    }
    pending_transform_preview_ = PendingTransformPreview{
        .kind = kind,
        .values = values,
        .binding_generation = runtime_binding_generation_,
        .selection_revision = document_->selection_revision(),
        .editor_mode_generation = editor_->mode().generation(),
    };
    if (!transform_dispatch_timer_->isActive()) {
      transform_dispatch_timer_->start();
    }
  }

  /// Publish at the timer deadline or synchronously before committing release.
  void flush_transform_preview() {
    const auto pending = std::exchange(pending_transform_preview_, std::nullopt);
    transform_dispatch_timer_->stop();
    if (!pending.has_value() || rebuilding_ || document_ == nullptr ||
        !property_context_current() ||
        pending->binding_generation != runtime_binding_generation_ ||
        pending->editor_mode_generation != editor_->mode().generation() ||
        pending->selection_revision != document_->selection_revision() ||
        !transform_edit_matches(pending->kind)) {
      return;
    }
    preview_transform_edit(pending->kind, pending->values);
  }

  /// Drop deferred work without touching a potentially outgoing document.
  void discard_pending_transform_preview() {
    pending_transform_preview_.reset();
    if (transform_dispatch_timer_ != nullptr) {
      transform_dispatch_timer_->stop();
    }
  }

  void finish_transform_edit(TransformEditKind kind) {
    const std::uint64_t generation = runtime_binding_generation_;
    QPointer<InspectorPanel> guard(&panel_);
    flush_transform_preview();
    if (guard == nullptr || generation != runtime_binding_generation_) {
      return;
    }
    if (!property_context_current()) {
      discard_pending_transform_preview();
      active_transform_preview_.abandon();
      active_transform_edit_.reset();
      force_rebuild_ = true;
      return;
    }
    if (document_ == nullptr ||
        document_->selection_revision() != built_scene_selection_revision_) {
      // Selection may have changed before the editor's queued focus-out. The
      // retained preview still owns its original targets; never retarget it.
      discard_pending_transform_preview();
      (void)commit_active_transform_edit();
      if (guard == nullptr || generation != runtime_binding_generation_) {
        return;
      }
      force_rebuild_ = true;
      return;
    }
    if (!transform_edit_matches(kind)) {
      return;
    }
    (void)commit_active_transform_edit();
    if (guard == nullptr || generation != runtime_binding_generation_) {
      return;
    }
    force_rebuild_ = true;
  }

  /// Accept only the existing preview's captured targets, not current selection.
  bool commit_active_transform_edit() {
    if (active_transform_preview_.preview_active()) {
      if (editor_ == nullptr) {
        return false;
      }
      const std::uint64_t generation = runtime_binding_generation_;
      const QPointer<InspectorPanel> guard(&panel_);
      const auto result = active_transform_preview_.commit(*editor_);
      if (guard == nullptr || generation != runtime_binding_generation_) {
        return false;
      }
      if (!result.ok()) {
        (void)editor_->ui().notify({.level = QuaderNotificationLevel::Error,
                                    .message = result.error_message()});
        if (guard == nullptr || generation != runtime_binding_generation_) {
          return false;
        }
        if (active_transform_preview_.preview_active()) {
          return false;
        }
      }
    }
    active_transform_edit_.reset();
    return true;
  }

  bool property_context_current() const {
    return editor_ != nullptr &&
        editor_->mode().generation() == built_editor_mode_generation_;
  }

  bool property_context_current(std::uint64_t property_generation) const {
    return property_generation == property_generation_ &&
        property_context_current();
  }

  void cancel_active_transform_edit() {
    discard_pending_transform_preview();
    if (active_transform_preview_.preview_active()) {
      (void)active_transform_preview_.cancel();
    }
    active_transform_edit_.reset();
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

    if (release_has_feature(ReleaseFeature::CastShadows)) {
      Section lighting =
          add_section("lighting", localized("ui.inspector.lighting", "Lighting"));
      add_checkbox(lighting, "lighting.cast_shadows",
                   localized("ui.inspector.cast_shadows", "Cast Shadows"),
                   mesh->cast_shadows, [this, entity](bool enabled) {
                     apply_mesh_component(entity, [enabled](MeshComponent &next) {
                       next.cast_shadows = enabled;
                     });
                   });
    }
  }

  void build_viewport_section() {
    std::vector<EntityId> meshes;
    meshes.reserve(selection_.size());
    BoolAggregate wireframe;
    BoolAggregate custom_color;
    std::optional<QVec3> first_color;
    bool mixed_color = false;
    for (const EntityId entity : selection_) {
      const MeshComponent *mesh = document_->mesh(entity);
      if (mesh == nullptr) {
        continue;
      }
      meshes.push_back(entity);
      wireframe.all_true = wireframe.all_true && mesh->wireframe_on_shaded;
      wireframe.any_true = wireframe.any_true || mesh->wireframe_on_shaded;
      custom_color.all_true = custom_color.all_true &&
                             mesh->wireframe_on_shaded_color.has_value();
      custom_color.any_true = custom_color.any_true ||
                             mesh->wireframe_on_shaded_color.has_value();
      const QVec3 color = mesh->wireframe_on_shaded_color.value_or(
          QVec3{presented_wireframe_color_.r, presented_wireframe_color_.g,
                presented_wireframe_color_.b});
      if (!first_color.has_value()) {
        first_color = color;
      } else {
        mixed_color = mixed_color || first_color->x != color.x ||
                      first_color->y != color.y || first_color->z != color.z;
      }
    }
    if (meshes.empty()) {
      return;
    }
    Section shading =
        add_section("viewport", localized("ui.inspector.viewport", "Viewport"));
    add_mixed_checkbox(
        shading, "viewport.wireframe_on_shaded",
        localized("ui.inspector.show_wireframe", "Show Wireframe"),
        wireframe.all_true, wireframe.mixed(), [this, meshes](bool enabled) {
          apply_mesh_components(meshes, [enabled](MeshComponent &next) {
            next.wireframe_on_shaded = enabled;
          }, "Set Mesh Shading");
        });
    add_mixed_checkbox(
        shading, "viewport.custom_wireframe_color",
        localized("ui.inspector.custom_wireframe_color", "Use Custom Color"),
        custom_color.all_true, custom_color.mixed(), [this, meshes](bool enabled) {
          const auto global =
              viewport_settings_->color(ViewportColorSettingId::WireframeOnShaded);
          apply_mesh_components(meshes, [enabled, global](MeshComponent &next) {
            if (!enabled) {
              next.wireframe_on_shaded_color.reset();
            } else if (!next.wireframe_on_shaded_color.has_value()) {
              next.wireframe_on_shaded_color = QVec3{global.r, global.g, global.b};
            }
          }, "Set Wireframe Color");
        });
    if (custom_color.any_true) {
      add_mesh_wireframe_color_button(shading, meshes, *first_color,
                                      custom_color.all_true, mixed_color);
    }
  }

  void add_mesh_wireframe_color_button(Section &section,
                                       const std::vector<EntityId> &meshes,
                                       QVec3 color, bool enabled, bool mixed) {
    const std::string stable_id = "viewport.wireframe_color";
    const QString label = localized("ui.inspector.wireframe_color", "Wireframe Color");
    auto *field = new QWidget(section.editor_parent);
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *button = new QtColorSwatchButton(field);
    button->setObjectName(stable_name(stable_id));
    button->setAccessibleName(label);
    button->set_swatch_color(QColor::fromRgbF(color.x, color.y, color.z));
    button->setEnabled(enabled);
    layout->addWidget(button, 1);
    if (mixed) {
      auto *mixed_label = new QLabel(localized("ui.inspector.mixed", "Mixed"), field);
      mixed_label->setObjectName(stable_name(stable_id + ".mixed"));
      layout->addWidget(mixed_label);
    }
    add_row(section, stable_id, label, field);
    const auto property_generation = property_generation_;
    QObject::connect(button, &QtColorSwatchButton::clicked, &panel_,
                     [this, button, meshes, stable_id, property_generation] {
      if (!property_context_current(property_generation) || document_ == nullptr) {
        return;
      }
      const auto binding_generation = runtime_binding_generation_;
      const auto mode_generation = editor_->mode().generation();
      std::vector<std::string> identities;
      identities.reserve(meshes.size());
      for (const EntityId entity : meshes) identities.push_back(document_->uuid(entity));
      const QPointer<InspectorPanel> panel_guard(&panel_);
      const QPointer<QtColorSwatchButton> button_guard(button);
      const QColor initial = button->swatch_color();
      const QPointer<QColorDialog> dialog = new QColorDialog(initial, &panel_);
      dialog->setObjectName(stable_name(stable_id + ".dialog"));
      dialog->setOption(QColorDialog::DontUseNativeDialog, true);
      dialog->setOption(QColorDialog::ShowAlphaChannel, false);
      active_color_dialog_ = dialog.data();
      active_mesh_color_dialog_ = true;
      QObject::connect(dialog.data(), &QColorDialog::currentColorChanged, dialog.data(),
                       [button_guard](const QColor &next) {
        if (button_guard != nullptr && next.isValid()) {
          button_guard->set_swatch_color(next);
        }
      });
      const int result = dialog->exec();
      if (panel_guard == nullptr) return;
      if (active_color_dialog_ == dialog.data()) {
        active_color_dialog_ = nullptr;
        active_mesh_color_dialog_ = false;
      }
      if (dialog == nullptr) return;
      const QColor accepted = dialog->selectedColor();
      delete dialog.data();
      if (runtime_binding_generation_ != binding_generation || editor_ == nullptr ||
          document_ == nullptr || editor_->mode().generation() != mode_generation) {
        return;
      }
      if (result != QDialog::Accepted) {
        if (button_guard != nullptr) button_guard->set_swatch_color(initial);
        return;
      }
      for (std::size_t index = 0; index < meshes.size(); ++index) {
        const MeshComponent *mesh = document_->mesh(meshes[index]);
        if (mesh == nullptr || !mesh->wireframe_on_shaded_color.has_value() ||
            document_->uuid(meshes[index]) != identities[index]) {
          return;
        }
      }
      if (!accepted.isValid()) return;
      const QVec3 rgb{static_cast<float>(accepted.redF()),
                     static_cast<float>(accepted.greenF()),
                     static_cast<float>(accepted.blueF())};
      apply_mesh_components(meshes, [rgb](MeshComponent &next) {
        next.wireframe_on_shaded_color = rgb;
      }, "Set Wireframe Color");
      force_rebuild_ = true;
    });
  }

  void build_physics_section() {
    std::vector<EntityId> meshes;
    meshes.reserve(selection_.size());
    BoolAggregate generate;
    BoolAggregate display;
    bool unsupported = false;
    for (const EntityId entity : selection_) {
      const MeshComponent *mesh = document_->mesh(entity);
      if (mesh == nullptr) {
        continue;
      }
      meshes.push_back(entity);
      generate.all_true = generate.all_true && mesh->generate_collisions;
      generate.any_true = generate.any_true || mesh->generate_collisions;
      display.all_true = display.all_true && mesh->display_collision;
      display.any_true = display.any_true || mesh->display_collision;
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
    add_mixed_checkbox(
        physics, "physics.display_collision",
        localized("ui.inspector.display_collision", "Display Collision"),
        display.all_true, display.mixed(), [this, meshes](bool enabled) {
          apply_mesh_components(meshes, [enabled](MeshComponent &next) {
            next.display_collision = enabled;
          });
        });
    if (unsupported) {
      auto *status = new QLabel(
          localized("ui.inspector.collision_unsupported",
                    "Collision generation is unavailable for this mesh’s saved physics settings."),
          physics.editor_parent);
      status->setObjectName(stable_name("physics.collision_status"));
      status->setWordWrap(true);
      add_row(physics, "physics.collision_status",
              localized("ui.inspector.collision_status", "Collision Status"),
              status);
    }
  }

  void build_light_section() {
    if (!release_has_feature(ReleaseFeature::Lights)) {
      return;
    }
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
    light_temperature_field_ = add_number_field(
        section, "light.color_temperature_kelvin",
        localized("ui.inspector.temperature", "Temperature (K)"),
        light->color_temperature_kelvin, kLightMinimumTemperatureKelvin,
        kLightMaximumTemperatureKelvin, 1.0,
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
        light_linear_to_srgb(light->color), entity);

    light_intensity_field_ = add_number_field(
        section, "light.intensity", localized("ui.inspector.intensity", "Intensity"),
        light->intensity, 0.0, std::numeric_limits<float>::max(), 0.01,
        [this, entity](double value, std::string merge_key) {
          apply_light_component(
              entity, [value](LightComponent &next) {
                next.intensity = static_cast<float>(value);
                next.source_preset = LightSourcePreset::Custom;
              }, std::move(merge_key));
        });
    if (light->type == LightType::Point || light->type == LightType::Spot) {
      light_range_field_ = add_continuous_number(
          section, "light.range", localized("ui.inspector.range", "Range"),
          light->range, kInspectorLightRangeMin,
          std::max(kInspectorLightRangeMax, light->range),
          0.1, 4, [this, entity](double value, std::string merge_key) {
            apply_light_component(
                entity,
                [value](LightComponent &next) {
                  next.range = static_cast<float>(value);
                },
                std::move(merge_key));
          });
      light_falloff_field_ = add_number_field(
          section, "light.distance_attenuation",
          localized("ui.inspector.distance_attenuation", "Falloff"),
          light->distance_attenuation, kLightMinimumFalloff,
          kLightMaximumFalloff, 0.01,
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
    if (release_has_feature(ReleaseFeature::Lights)) {
      add_read_only_value(section, "components.lights",
                          localized("ui.inspector.lights", "Lights"),
                          QString::number(static_cast<qulonglong>(light_count)));
    }
  }

  InspectorPanel &panel_;
  QPointer<QWidget> component_extension_;
  InspectorPropertyTree *tree_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  const SceneDocument *document_ = nullptr;
  const ModelingSessionService *modeling_ = nullptr;
  LocalizationService *localization_ = nullptr;
  const ViewportSettings *viewport_settings_ = nullptr;
  ViewportSettingColor presented_wireframe_color_{0.0f, 0.0f, 0.0f, 1.0f};
  SceneDocument *preview_document_ = nullptr;
  const AssetRegistry *preview_assets_ = nullptr;
  InspectorMaterialPresentationSnapshot material_presentation_;
  std::vector<MaterialRow> material_rows_;
  std::vector<SectionHeader> section_headers_;
  std::vector<PropertyRow> property_rows_;
  std::vector<TransformRow> transform_rows_;
  QPointer<QDoubleSpinBox> light_range_field_;
  NumberField light_intensity_field_;
  NumberField light_falloff_field_;
  NumberField light_temperature_field_;
  QPointer<QtColorSwatchButton> light_color_button_;
  QPointer<QComboBox> light_preset_combo_;
  QPointer<QComboBox> light_color_mode_combo_;
  std::optional<LightComponent> presented_light_;
  SceneTransformDragService active_transform_preview_;
  std::optional<TransformEditState> active_transform_edit_;
  std::optional<PendingTransformPreview> pending_transform_preview_;
  QTimer *transform_dispatch_timer_ = nullptr;
  UIContinuousEditTracker active_continuous_edit_;
  QColorDialog *active_color_dialog_ = nullptr;
  bool active_mesh_color_dialog_ = false;
  std::vector<EntityId> selection_;
  EntityId primary_ = kInvalidEntityId;
  std::unordered_map<std::string, bool> expanded_sections_;
  std::uint64_t built_scene_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_light_property_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_transform_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t presented_transform_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_scene_selection_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_modeling_content_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_modeling_selection_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_editor_mode_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t inspected_mode_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t inspected_selection_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_editor_mode_generation_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t built_material_presentation_revision_ =
      std::numeric_limits<std::uint64_t>::max();
  std::uint64_t runtime_binding_generation_ = 0;
  std::uint64_t property_generation_ = 0;
  QtUiScaleMetrics ui_scale_metrics_;
  int scaled_default_label_column_width_ = 140;
  int preferred_label_column_width_ = 0;
  std::string built_locale_;
  std::string active_continuous_id_;
  bool continuous_edit_active_ = false;
  bool rebuilding_ = false;
  bool force_rebuild_ = true;
  bool adjusting_label_column_width_ = false;
  bool property_layout_refresh_pending_ = false;
  bool scale_linked_ = false;
};

InspectorPanel::InspectorPanel(QWidget *parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(*this)) {}

InspectorPanel::~InspectorPanel() = default;

void InspectorPanel::set_component_extension(QWidget *extension) {
  impl_->set_component_extension(extension);
}

void InspectorPanel::set_material_presentation(
    InspectorMaterialPresentationSnapshot snapshot) {
  impl_->set_material_presentation(std::move(snapshot));
}

void InspectorPanel::abandon_runtime() noexcept { impl_->abandon_runtime(); }

bool InspectorPanel::settle_pending_edits() {
  return impl_->settle_pending_edits();
}

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

void InspectorPanel::refresh_transform_preview(UIContext &context) {
  impl_->refresh_transform_preview(context);
}

} // namespace quader
