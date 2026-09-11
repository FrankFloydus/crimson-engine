////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tree_relationship_presentation.hpp"

#include "ui/qt_ui_scale_metrics.hpp"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QProxyStyle>
#include <QStyleOption>
#include <QTreeView>

#include <algorithm>

namespace quader {
namespace {

class QtTreeRelationshipStyle final : public QProxyStyle {
public:
  explicit QtTreeRelationshipStyle(QTreeView &tree)
      : QProxyStyle(QStringLiteral("fusion")) {
    setObjectName(QStringLiteral("quaderTreeRelationshipPresentation"));
    setParent(&tree);
    refresh_palette_policy();
  }

  bool set_ui_scale(const QtUiScaleMetrics &metrics) {
    if (scaled_metrics_style_.metrics().factor() == metrics.factor() &&
        scaled_metrics_style_.metrics().revision() == metrics.revision()) {
      return false;
    }
    scaled_metrics_style_.set_metrics(metrics);
    return true;
  }

  int pixelMetric(PixelMetric metric, const QStyleOption *option,
                  const QWidget *widget) const override {
    return scaled_metrics_style_.pixelMetric(metric, option, widget);
  }

  void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget = nullptr) const override {
    if (element != PE_IndicatorBranch || option == nullptr ||
        painter == nullptr || option->rect.isEmpty()) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }

    constexpr State kRelationshipStates =
        State_Item | State_Sibling | State_Children | State_Open;
    if (option->state.testAnyFlags(kRelationshipStates)) {
      draw_relationships(*option, *painter);
    }

    // Fusion owns the disclosure glyph, including open/closed and RTL arrows.
    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event != nullptr &&
        (event->type() == QEvent::PaletteChange ||
         event->type() == QEvent::ApplicationPaletteChange) &&
        refresh_palette_policy()) {
      if (auto *tree = qobject_cast<QTreeView *>(watched)) {
        tree->viewport()->update();
      }
    }
    return QProxyStyle::eventFilter(watched, event);
  }

private:
  bool refresh_palette_policy() {
    const bool platform_palette_is_authoritative =
        QApplication::palette().resolveMask() == 0;
    if (platform_palette_is_authoritative_ ==
        platform_palette_is_authoritative) {
      return false;
    }
    platform_palette_is_authoritative_ = platform_palette_is_authoritative;
    return true;
  }

  void draw_relationships(const QStyleOption &option,
                          QPainter &painter) const {
    const QRect bounds = option.rect;
    const int center_x = bounds.center().x();
    const int center_y = bounds.center().y();
    const bool has_children = option.state.testFlag(State_Children);
    const bool has_sibling = option.state.testFlag(State_Sibling);
    const bool right_to_left = option.direction == Qt::RightToLeft;

    int indicator_half_extent = 0;
    if (has_children) {
      const int maximum_extent =
          std::max(1, std::min(bounds.width() - 4, bounds.height() - 2));
      int extent = std::clamp(9, 1, maximum_extent);
      if (extent % 2 == 0) {
        --extent;
      }
      indicator_half_extent = extent / 2;
    }

    const QPalette::ColorGroup group =
        !option.state.testFlag(State_Enabled)
            ? QPalette::Disabled
            : option.state.testFlag(State_Active) ? QPalette::Active
                                                  : QPalette::Inactive;
    const QPalette::ColorRole connector_role =
        platform_palette_is_authoritative_ ? QPalette::Text
                                           : QPalette::Midlight;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    QPen pen(option.palette.color(group, connector_role));
    pen.setWidth(0);
    pen.setCapStyle(Qt::FlatCap);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    const int top_stop =
        has_children ? center_y - indicator_half_extent - 1 : center_y;
    if (top_stop >= bounds.top()) {
      painter.drawLine(center_x, bounds.top(), center_x, top_stop);
    }
    if (has_sibling) {
      const int bottom_start =
          has_children ? center_y + indicator_half_extent + 1 : center_y;
      if (bottom_start <= bounds.bottom()) {
        // Cosmetic flat-cap lines exclude the final raster endpoint. Extend
        // through the clip edge so vertically adjacent branch cells join.
        painter.drawLine(center_x, bottom_start, center_x,
                         bounds.bottom() + 1);
      }
    }

    if (option.state.testFlag(State_Item)) {
      const int inner = has_children
                            ? center_x + (right_to_left
                                              ? -indicator_half_extent - 1
                                              : indicator_half_extent + 1)
                            : center_x;
      const int outer =
          right_to_left ? bounds.left() : bounds.right() + 1;
      if ((!right_to_left && inner <= outer) ||
          (right_to_left && inner >= outer)) {
        painter.drawLine(inner, center_y, outer, center_y);
      }
    }

    painter.restore();
  }

  QtUiScaleProxyStyle scaled_metrics_style_;
  bool platform_palette_is_authoritative_ = false;
};

QtTreeRelationshipStyle *relationship_style(QTreeView &tree) {
  return dynamic_cast<QtTreeRelationshipStyle *>(tree.style());
}

void apply_tree_indentation(QTreeView &tree,
                            const QtTreeRelationshipStyle &style) {
  tree.setIndentation(
      style.pixelMetric(QStyle::PM_TreeViewIndentation, nullptr, &tree));
  tree.doItemsLayout();
  tree.viewport()->update();
}

} // namespace

void install_qt_tree_relationship_presentation(QTreeView &tree) {
  if (relationship_style(tree) != nullptr) {
    return;
  }
  auto *style = new QtTreeRelationshipStyle(tree);
  tree.setStyle(style);
  tree.installEventFilter(style);
  apply_tree_indentation(tree, *style);
}

void apply_ui_scale_to_qt_tree_relationship_presentation(
    QTreeView &tree, const QtUiScaleMetrics &metrics) {
  QtTreeRelationshipStyle *style = relationship_style(tree);
  if (style == nullptr) {
    install_qt_tree_relationship_presentation(tree);
    style = relationship_style(tree);
  }
  if (style != nullptr && style->set_ui_scale(metrics)) {
    apply_tree_indentation(tree, *style);
  }
}

} // namespace quader
