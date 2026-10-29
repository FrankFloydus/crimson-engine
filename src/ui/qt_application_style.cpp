////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_application_style.hpp"

#include "ui/qt_application_theme.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QDockWidget>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QMenu>
#include <QPainter>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOption>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>
#include <QVariantAnimation>

#include <algorithm>
#include <cmath>

namespace quader {
namespace {

QPalette::ColorGroup color_group(const QStyleOption &option) {
  if (!option.state.testFlag(QStyle::State_Enabled)) {
    return QPalette::Disabled;
  }
  return option.state.testFlag(QStyle::State_Active) ? QPalette::Active
                                                   : QPalette::Inactive;
}

QColor color(const QStyleOption &option, QPalette::ColorRole role) {
  return option.palette.color(color_group(option), role);
}

QStyleOption menu_glyph_option(const QStyleOption &option,
                              const QWidget *widget) {
  QStyleOption glyph(option);
  if (qobject_cast<const QMenu *>(widget) != nullptr &&
      glyph.palette.color(QPalette::Highlight).alpha() == 0) {
    // The native row's transparent selection must not bleed into the
    // backgrounds of its checked indicators or selected, checked icons.
    glyph.palette.setBrush(QPalette::Highlight,
                           widget->palette().brush(color_group(option),
                                                   QPalette::Highlight));
    glyph.palette.setBrush(QPalette::Button,
                           widget->palette().brush(color_group(option),
                                                   QPalette::Button));
  }
  return glyph;
}

QColor blend(const QColor &from, const QColor &to, qreal amount) {
  return QColor::fromRgbF(
      from.redF() + (to.redF() - from.redF()) * amount,
      from.greenF() + (to.greenF() - from.greenF()) * amount,
      from.blueF() + (to.blueF() - from.blueF()) * amount,
      from.alphaF() + (to.alphaF() - from.alphaF()) * amount);
}

qreal hairline(const QPainter &painter) {
  return 1.0 / std::max<qreal>(
                   1.0, painter.device() != nullptr
                            ? painter.device()->devicePixelRatioF()
                            : 1.0);
}

bool enabled(const QStyleOption &option) {
  return option.state.testFlag(QStyle::State_Enabled);
}

bool hovered(const QStyleOption &option) {
  return enabled(option) && option.state.testFlag(QStyle::State_MouseOver);
}

bool focused(const QStyleOption &option) {
  return enabled(option) && option.state.testFlag(QStyle::State_HasFocus);
}

bool keyboard_focused(const QStyleOption &option) {
  return focused(option) &&
         option.state.testFlag(QStyle::State_KeyboardFocusChange);
}

qreal switch_position(Qt::CheckState state) {
  return state == Qt::PartiallyChecked ? 0.5 : state == Qt::Checked ? 1.0 : 0.0;
}

void animate_switch(QVariantAnimation &animation, const QWidget &widget,
                    qreal target) {
  const qreal start = animation.currentValue().toReal();
  animation.stop();
  animation.setStartValue(start);
  animation.setEndValue(target);
  if (widget.isVisible() && widget.isEnabled()) {
    animation.start();
  } else {
    animation.setCurrentTime(animation.duration());
  }
}

qreal animated_switch_position(const QStyleOption &option,
                                const QWidget *widget) {
  const qreal target = option.state.testFlag(QStyle::State_NoChange) ? 0.5
                      : option.state.testFlag(QStyle::State_On) ? 1.0 : 0.0;
  auto *checkbox = const_cast<QCheckBox *>(qobject_cast<const QCheckBox *>(widget));
  if (checkbox == nullptr) {
    return target;
  }
  constexpr auto kAnimationName = "quaderCheckSwitchAnimation";
  auto *animation = checkbox->findChild<QVariantAnimation *>(
      QString::fromLatin1(kAnimationName), Qt::FindDirectChildrenOnly);
  if (animation == nullptr) {
    animation = new QVariantAnimation(checkbox);
    animation->setObjectName(QString::fromLatin1(kAnimationName));
    animation->setDuration(180);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->setStartValue(target);
    animation->setEndValue(target);
    QObject::connect(animation, &QVariantAnimation::valueChanged, checkbox,
                     [checkbox](const QVariant &) { checkbox->update(); });
    QObject::connect(checkbox, &QCheckBox::checkStateChanged, animation,
                     [animation, checkbox](Qt::CheckState state) {
                       animate_switch(*animation, *checkbox, switch_position(state));
                     });
  }
  if (animation->endValue().toReal() != target) {
    // Retained owners can synchronize check state while signals are blocked.
    animate_switch(*animation, *checkbox, target);
  }
  if (!enabled(option) && animation->state() == QAbstractAnimation::Running) {
    animation->stop();
    animation->setCurrentTime(animation->duration());
  }
  return animation->currentValue().toReal();
}

QColor surface_color(const QStyleOption &option, bool field) {
  // Raised editors stay distinct from the section surface. Spin boxes and
  // their frameless editors share the same inherited control color.
  QColor surface = color(option, QPalette::Button);
  if (!field && (option.state.testFlag(QStyle::State_On) ||
                 option.state.testFlag(QStyle::State_NoChange))) {
    surface = color(option, QPalette::Highlight);
  }
  if (!field && enabled(option) && option.state.testFlag(QStyle::State_Sunken)) {
    return blend(surface, color(option, QPalette::Shadow), 0.24);
  }
  return hovered(option)
             ? blend(surface, color(option, QPalette::WindowText), 0.045)
             : surface;
}

void rounded_surface(QPainter &painter, const QRect &rect, const QColor &fill,
                     const QColor &border, qreal radius) {
  const qreal width = hairline(painter);
  const QRectF bounds = QRectF(rect).adjusted(width * 0.5, width * 0.5,
                                             -width * 0.5, -width * 0.5);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(border, width));
  painter.setBrush(fill);
  const qreal usable_radius =
      std::min(radius, std::min(bounds.width(), bounds.height()) * 0.5);
  if (usable_radius > 0.0) {
    painter.drawRoundedRect(bounds, usable_radius, usable_radius);
  } else {
    painter.drawRect(bounds);
  }
  painter.restore();
}

void draw_surface(QPainter &painter, const QStyleOption &option,
                  const QtUiScaleMetrics &metrics, bool field,
                  bool default_button = false,
                  const QWidget *editor = nullptr) {
  const bool square = editor != nullptr &&
                      editor->property(kQtSquareEditorSurfaceProperty).toBool();
  QColor fill = surface_color(option, field);
  QColor border = square ? color(option, QPalette::Mid)
                         : field ? color(option, QPalette::Midlight)
                                 : blend(fill, color(option, QPalette::Dark), 0.4);
  if (default_button && enabled(option)) {
    // Inactive text is dimmer and its accent lighter. Keep the native label
    // readable even at the brightest enabled hover surface in either group.
    const int darkening = color_group(option) == QPalette::Inactive ? 190 : 150;
    fill = color(option, QPalette::Accent).darker(darkening);
    if (option.state.testFlag(QStyle::State_Sunken)) {
      fill = fill.darker(112);
    } else if (hovered(option)) {
      fill = fill.lighter(110);
    }
  }
  if (focused(option) || (default_button && enabled(option))) {
    border = color(option, QPalette::Accent);
  } else if (hovered(option)) {
    border = color(option, QPalette::Light);
  }
  rounded_surface(painter, option.rect, fill, border,
                  square ? 0.0 : metrics.px(4.0));
  if (field && square) {
    const QVariant axis_property = editor->property(kQtEditorAxisProperty);
    bool valid_axis = false;
    const int axis_value = axis_property.toInt(&valid_axis);
    using quader_editor_core::ViewAxis;
    if (valid_axis && axis_value >= static_cast<int>(ViewAxis::X) &&
        axis_value <= static_cast<int>(ViewAxis::Z)) {
      QColor axis_color =
          qt_semantic_axis_color(static_cast<ViewAxis>(axis_value));
      if (!enabled(option)) {
        axis_color = blend(fill, axis_color, 0.45);
      }
      const qreal inset = hairline(painter);
      const QRectF interior =
          QRectF(option.rect).adjusted(inset, inset, -inset, -inset);
      const qreal strip_width = std::min(metrics.px(2.0), interior.width());
      if (strip_width > 0.0 && interior.height() > 0.0) {
        const qreal strip_left = option.direction == Qt::RightToLeft
                                     ? interior.right() - strip_width
                                     : interior.left();
        painter.fillRect(QRectF(strip_left, interior.top(), strip_width,
                                interior.height()), axis_color);
      }
    }
  }
}

void draw_check_switch(QPainter &painter, const QStyleOption &option,
                       const QtUiScaleMetrics &metrics, const QWidget *widget) {
  const bool checked = option.state.testFlag(QStyle::State_On);
  const bool partial = option.state.testFlag(QStyle::State_NoChange);
  const qreal position = animated_switch_position(option, widget);
  const qreal width = metrics.px(static_cast<double>(kQtCheckSwitchWidth));
  const qreal height = metrics.px(static_cast<double>(kQtCheckSwitchHeight));
  const qreal fit = std::min({1.0, option.rect.width() / width,
                             option.rect.height() / height});
  if (fit <= 0.0) {
    return;
  }
  const QPointF center = QRectF(option.rect).center();
  const qreal edge = hairline(painter);
  const QRectF track(
      std::round((center.x() - width * fit * 0.5) / edge) * edge,
      std::round((center.y() - height * fit * 0.5) / edge) * edge,
      width * fit, height * fit);
  const QPointF track_center = track.center();
  const bool muted_track = partial || (checked && !enabled(option));
  QColor fill = muted_track ? color(option, QPalette::Highlight)
                            : blend(color(option, QPalette::Button),
                                    color(option, QPalette::Accent), position);
  if (enabled(option) && option.state.testFlag(QStyle::State_Sunken)) {
    fill = blend(fill, color(option, QPalette::Shadow), 0.24);
  } else if (hovered(option)) {
    fill = blend(fill, color(option, QPalette::WindowText), 0.08);
  }
  QColor border = checked ? (enabled(option) ? fill
                                            : color(option, QPalette::Accent))
                          : color(option, QPalette::Midlight);
  if (keyboard_focused(option)) {
    border = color(option, checked ? QPalette::Text : QPalette::Accent);
  } else if (hovered(option) && !checked) {
    border = color(option, QPalette::Light);
  }
  const QRectF outline = track.adjusted(edge * 0.5, edge * 0.5,
                                       -edge * 0.5, -edge * 0.5);
  const qreal inset = metrics.px(1.0) * fit;
  const qreal diameter = std::max(0.0, track.height() - inset * 2.0);
  const qreal travel = std::max(0.0, track.width() - inset * 2.0 - diameter);
  const qreal thumb_position = option.direction == Qt::RightToLeft
                                   ? 1.0 - position : position;
  const qreal thumb_left = track.left() + inset + travel * thumb_position;
  const QRectF thumb(thumb_left, track_center.y() - diameter * 0.5,
                     diameter, diameter);
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(border, edge));
  painter.setBrush(fill);
  painter.drawRoundedRect(outline, outline.height() * 0.5,
                          outline.height() * 0.5);
  painter.setPen(Qt::NoPen);
  painter.setBrush(color(option, QPalette::Text));
  painter.drawEllipse(thumb);
  if (partial) {
    QPen dash(color(option, QPalette::Base),
               std::max(edge, metrics.px(1.0) * fit));
    dash.setCapStyle(Qt::RoundCap);
    painter.setPen(dash);
    painter.drawLine(QPointF(thumb.left() + diameter * 0.3, track_center.y()),
                      QPointF(thumb.right() - diameter * 0.3, track_center.y()));
  }
  painter.restore();
}

void draw_slider(QPainter &painter, const QStyleOptionSlider &option,
                 const QStyle &style, const QtUiScaleMetrics &metrics,
                 const QWidget *widget) {
  const QRect handle = style.subControlRect(
      QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, widget);
  if (handle.isEmpty()) {
    return;
  }
  // Native geometry owns travel, inversion and hit areas. Normalized endpoint
  // queries keep the full rail visible even when the actual range is fixed.
  QStyleOptionSlider endpoint(option);
  endpoint.minimum = 0;
  endpoint.maximum = 1;
  endpoint.sliderPosition = 0;
  const QPointF start = QRectF(style.subControlRect(
      QStyle::CC_Slider, &endpoint, QStyle::SC_SliderHandle, widget)).center();
  endpoint.sliderPosition = 1;
  const QPointF end = QRectF(style.subControlRect(
      QStyle::CC_Slider, &endpoint, QStyle::SC_SliderHandle, widget)).center();
  const QPointF center = QRectF(handle).center();
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  if (option.subControls.testFlag(QStyle::SC_SliderGroove)) {
    QPen rail(color(option, QPalette::Midlight), metrics.px(2.0));
    rail.setCapStyle(Qt::RoundCap);
    painter.setPen(rail);
    painter.drawLine(start, end);
    if (option.maximum > option.minimum && start != center) {
      rail.setColor(color(option, QPalette::Accent));
      painter.setPen(rail);
      painter.drawLine(start, center);
    }
  }
  if (option.subControls.testFlag(QStyle::SC_SliderHandle)) {
    const qreal diameter = std::min({metrics.px(8.0),
                                    static_cast<qreal>(handle.width()),
                                    static_cast<qreal>(handle.height())});
    const QRectF dot(center.x() - diameter * 0.5,
                      center.y() - diameter * 0.5, diameter, diameter);
    QColor fill = color(option, QPalette::Text);
    const bool active = option.activeSubControls.testFlag(QStyle::SC_SliderHandle);
    if (enabled(option) && active &&
        option.state.testFlag(QStyle::State_Sunken)) {
      fill = blend(fill, color(option, QPalette::WindowText), 0.25);
    } else if (active && hovered(option)) {
      fill = blend(fill, color(option, QPalette::HighlightedText), 0.5);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawEllipse(dot);
    if (focused(option)) {
      const qreal inset = metrics.px(2.0);
      painter.setPen(QPen(color(option, QPalette::Accent), hairline(painter)));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(dot.adjusted(-inset, -inset, inset, inset));
    }
  }
  painter.restore();
}

void draw_radio_indicator(QPainter &painter, const QStyleOption &option) {
  const bool checked = option.state.testFlag(QStyle::State_On);
  const bool partial = option.state.testFlag(QStyle::State_NoChange);
  QColor border = color(option, QPalette::Midlight);
  if (keyboard_focused(option) || hovered(option) || checked || partial) {
    border = color(option, QPalette::Accent);
  }
  const QColor fill = surface_color(option, !checked && !partial);
  rounded_surface(painter, option.rect, fill, border, option.rect.height() * 0.5);
  if (!checked && !partial) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF bounds(option.rect);
  const QColor mark = color(option, QPalette::Accent);
  painter.setPen(Qt::NoPen);
  painter.setBrush(mark);
  painter.drawEllipse(bounds.adjusted(bounds.width() * 0.29,
                                      bounds.height() * 0.29,
                                      -bounds.width() * 0.29,
                                      -bounds.height() * 0.29));
  painter.restore();
}

void draw_arrow(QStyle::PrimitiveElement element, const QStyleOption &option,
                const QRect &rect, QPainter &painter, const QStyle &style,
                const QtUiScaleMetrics &metrics, const QWidget *widget) {
  QStyleOption arrow(option);
  arrow.palette.setCurrentColorGroup(color_group(arrow));
  const int extent = std::min({metrics.px(10), rect.width(), rect.height()});
  arrow.rect = QRect(0, 0, extent, extent);
  arrow.rect.moveCenter(rect.center());
  style.drawPrimitive(element, &arrow, &painter, widget);
}

qreal centered_text_offset(const QFont &source_font, const QRect &rect,
                           QStringView text) {
  QString visible;
  visible.reserve(text.size());
  for (qsizetype index = 0; index < text.size(); ++index) {
    if (text[index] != QLatin1Char('&')) {
      visible += text[index];
    } else if (index + 1 < text.size() &&
               text[index + 1] == QLatin1Char('&')) {
      visible += QLatin1Char('&');
      ++index;
    }
  }
  if (visible.isEmpty()) {
    return 0.0;
  }
  QFont font(source_font);
  font.setPointSizeF(QFontInfo(source_font).pointSizeF());
  const QFontMetricsF metrics(font);
  const QRectF row(rect);
  const QRectF line = metrics.boundingRect(
      row, Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextShowMnemonic,
      text.toString());
  const QRectF ink = metrics.tightBoundingRect(visible);
  const qreal baseline = line.top() + metrics.ascent();
  return row.center().y() - (baseline + ink.center().y());
}

qreal centered_menu_text_offset(const QStyleOptionMenuItem &option,
                                QStringView text) {
  QFont font(option.font);
  if (option.menuItemType == QStyleOptionMenuItem::DefaultItem) {
    font.setBold(true);
  }
  return centered_text_offset(font, option.rect, text);
}

} // namespace

bool qt_application_style_active(const QStyleOption *option) {
  if (option == nullptr || QApplication::palette().resolveMask() == 0) {
    return false;
  }
  // High Contrast deliberately clears the application override. Also leave
  // explicitly light widget palettes to Fusion, such as native preview tools.
  const QColor window = option->palette.color(QPalette::Window);
  return QApplication::palette().color(QPalette::Window).lightness() < 128 &&
         window.lightness() < 128 &&
         option->palette.color(QPalette::WindowText).lightness() >
             window.lightness() + 40;
}

bool qt_draw_application_primitive(
    QStyle::PrimitiveElement element, const QStyleOption *option,
    QPainter *painter, const QStyle &, const QtUiScaleMetrics &metrics,
    const QWidget *widget) {
  if (painter == nullptr || !qt_application_style_active(option)) {
    return false;
  }
  switch (element) {
  case QStyle::PE_IndicatorDockWidgetResizeHandle:
    painter->fillRect(option->rect,
                      color(*option, hovered(*option) ? QPalette::Light
                                                     : QPalette::Mid));
    return true;
  case QStyle::PE_PanelButtonCommand:
  case QStyle::PE_PanelButtonTool: {
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    const auto *tool_button = qobject_cast<const QToolButton *>(widget);
    const bool toolbar_button =
        tool_button != nullptr &&
        qobject_cast<const QToolBar *>(widget->parentWidget()) != nullptr;
    QStyleOption face = menu_glyph_option(*option, widget);
    if (!face.state.testFlag(QStyle::State_KeyboardFocusChange)) {
      face.state.setFlag(QStyle::State_HasFocus, false);
    }
    const bool flat = option->state.testFlag(QStyle::State_AutoRaise) ||
                      toolbar_button ||
                      (button != nullptr &&
                       button->features.testFlag(QStyleOptionButton::Flat));
    if (flat && !hovered(face) && !focused(face) &&
        !option->state.testFlag(QStyle::State_On) &&
        !option->state.testFlag(QStyle::State_Sunken)) {
      return true;
    }
    draw_surface(*painter, face, metrics, false,
                  button != nullptr && button->features.testFlag(
                                           QStyleOptionButton::DefaultButton));
    return true;
  }
  case QStyle::PE_FrameButtonTool:
    // The panel surface already owns its complete border and focus state.
    return true;
  case QStyle::PE_PanelLineEdit:
  case QStyle::PE_FrameLineEdit: {
    const auto *frame = qstyleoption_cast<const QStyleOptionFrame *>(option);
    if (frame != nullptr && frame->lineWidth == 0) {
      if (element == QStyle::PE_PanelLineEdit &&
          (widget == nullptr ||
           !widget->property(kQtParentPaintedEditorSurfaceProperty).toBool())) {
        painter->fillRect(option->rect, surface_color(*option, true));
      }
      return true;
    }
    draw_surface(*painter, *option, metrics, true, false, widget);
    return true;
  }
  case QStyle::PE_IndicatorCheckBox:
  case QStyle::PE_IndicatorItemViewItemCheck:
    draw_check_switch(*painter, menu_glyph_option(*option, widget), metrics, widget);
    return true;
  case QStyle::PE_IndicatorRadioButton:
    draw_radio_indicator(*painter, menu_glyph_option(*option, widget));
    return true;
  case QStyle::PE_IndicatorTabClose: {
    const QWidget *tab_bar = qobject_cast<const QTabBar *>(widget);
    if (tab_bar == nullptr && widget != nullptr) {
      tab_bar = qobject_cast<const QTabBar *>(widget->parentWidget());
    }
    const bool scene_tab = tab_bar != nullptr &&
                            tab_bar->property(kQtConnectedTabSurfaceProperty)
                                .toBool();
    const bool pressed = enabled(*option) &&
                         option->state.testFlag(QStyle::State_Sunken);
    if (hovered(*option) || pressed || focused(*option)) {
      draw_surface(*painter, *option, metrics, false);
    }
    const bool emphasized = enabled(*option) &&
                            (hovered(*option) || pressed ||
                             option->state.testFlag(QStyle::State_Selected));
    const qreal extent = std::min<qreal>(
        metrics.px(scene_tab ? 5.0 : 8.0),
        std::min(option->rect.width(), option->rect.height()) * 0.5);
    const QPointF center = QRectF(option->rect).center();
    const QRectF glyph(center.x() - extent * 0.5,
                       center.y() - extent * 0.5, extent, extent);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(color(*option, emphasized ? QPalette::Text : QPalette::WindowText),
             std::max<qreal>(hairline(*painter),
                              metrics.px(scene_tab ? 1.1 : 1.4)));
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->drawLine(glyph.topLeft(), glyph.bottomRight());
    painter->drawLine(glyph.topRight(), glyph.bottomLeft());
    painter->restore();
    return true;
  }
  case QStyle::PE_FrameFocusRect: {
    // Fusion skips idle flat/auto-raise panels entirely. Their focus primitive
    // remains the only keyboard cue. Default buttons also keep native focus
    // because their outer accent border is already present at rest.
    const auto *push_button = qobject_cast<const QPushButton *>(widget);
    const auto *tool_button = qobject_cast<const QToolButton *>(widget);
    if (qobject_cast<const QAbstractButton *>(widget) != nullptr &&
        !option->state.testFlag(QStyle::State_KeyboardFocusChange)) {
      return true;
    }
    if ((push_button != nullptr && !push_button->isFlat() &&
         !push_button->isDefault()) ||
        (tool_button != nullptr && !tool_button->autoRaise())) {
      return true;
    }
    return false;
  }
  case QStyle::PE_PanelMenu: {
    if (qobject_cast<const QMenu *>(widget) == nullptr) {
      return false;
    }
    // QMenu deliberately clears its panel state before painting. Resolve the
    // popup surface from the widget, not that state-less panel option.
    QStyleOption panel;
    panel.initFrom(widget);
    rounded_surface(*painter, option->rect,
                    color(panel, QPalette::AlternateBase),
                    color(panel, QPalette::Midlight), metrics.px(6.0));
    return true;
  }
  case QStyle::PE_FrameMenu:
    // The panel already owns the outline; a later native frame would square
    // its corners. Do not repaint the panel over its menu rows here.
    return qobject_cast<const QMenu *>(widget) != nullptr;
  default:
    return false;
  }
}

bool qt_draw_application_control(
    QStyle::ControlElement element, const QStyleOption *option,
    QPainter *painter, const QStyle &style, const QtUiScaleMetrics &metrics,
    const QWidget *widget) {
  if (painter == nullptr || option == nullptr) {
    return false;
  }
  const bool themed = qt_application_style_active(option);
  if (!themed && element != QStyle::CE_DockWidgetTitle) {
    return false;
  }
  switch (element) {
  case QStyle::CE_DockWidgetTitle: {
    const auto *dock = qstyleoption_cast<const QStyleOptionDockWidget *>(option);
    if (dock == nullptr) {
      return false;
    }
    const auto *dock_widget = qobject_cast<const QDockWidget *>(widget);
    const QIcon icon = dock_widget != nullptr
                           ? qt_dock_title_panel_icon(
                                 *dock_widget, dock->palette, color_group(*dock))
                           : QIcon{};
    const auto *proxy = qobject_cast<const QProxyStyle *>(&style);
    if (!themed && (icon.isNull() || proxy == nullptr)) {
      return false;
    }
    // Keep Qt's title allocation, including button space and RTL placement.
    // Styling the title subcontrol with QSS bypasses its foreground/font rules.
    QRect text_rect = style.subElementRect(
        QStyle::SE_DockWidgetTitleBarText, dock, widget);
    QRect bounds = dock->rect;
    painter->save();
    if (!themed) {
      QStyleOptionDockWidget native_title(*dock);
      native_title.title.clear();
      proxy->QProxyStyle::drawControl(element, &native_title, painter, widget);
    }
    if (dock->verticalTitleBar) {
      painter->translate(bounds.left(), bounds.bottom() + 1);
      painter->rotate(-90);
      text_rect = QRect(bounds.bottom() - text_rect.bottom(),
                        text_rect.left() - bounds.left(),
                        text_rect.height(), text_rect.width());
      bounds = QRect(0, 0, bounds.height(), bounds.width());
    }
    // QDockWidget seeds its painter with a platform title font that does not
    // follow our application scale controller. Use the scaled widget font.
    QFont title_font = themed && widget != nullptr ? widget->font() : painter->font();
    if (themed) {
      title_font.setWeight(QFont::DemiBold);
    }
    const QFontMetrics title_metrics(title_font);
    QRect logical_text = QStyle::visualRect(dock->direction, bounds, text_rect);
    const int inset = metrics.px(2);
    logical_text.adjust(inset, 0, -inset, 0);
    if (themed) {
      painter->fillRect(bounds, color(*dock, QPalette::AlternateBase));
      const qreal edge = hairline(*painter);
      painter->fillRect(QRectF(bounds.left(), bounds.bottom() + 1 - edge,
                               bounds.width(), edge),
                         color(*dock, QPalette::Mid));
    }
    if (!icon.isNull()) {
      const int extent = std::max(0, std::min(
          {metrics.px(16), logical_text.height(), logical_text.width()}));
      const QRect logical_icon(logical_text.left(),
          logical_text.top() + (logical_text.height() - extent) / 2,
          extent, extent);
      icon.paint(painter, QStyle::visualRect(dock->direction, bounds, logical_icon),
                 Qt::AlignCenter, QIcon::Normal);
      logical_text.setLeft(logical_text.left() + extent + metrics.px(6));
    }
    const QString title = title_metrics.elidedText(
        dock->title, Qt::ElideRight, std::max(0, logical_text.width()));
    text_rect = QStyle::visualRect(dock->direction, bounds, logical_text);
    painter->setFont(title_font);
    painter->setPen(color(*dock, themed ? QPalette::Text : QPalette::WindowText));
    painter->setClipRect(text_rect, Qt::IntersectClip);
    if (!icon.isNull()) {
      painter->translate(0.0, centered_text_offset(title_font, text_rect, title));
    }
    painter->drawText(text_rect,
                     QStyle::visualAlignment(dock->direction,
                                             Qt::AlignLeft | Qt::AlignVCenter) |
                         Qt::TextHideMnemonic,
                     title);
    painter->restore();
    return true;
  }
  case QStyle::CE_MenuItem: {
    const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
    const auto *proxy = qobject_cast<const QProxyStyle *>(&style);
    if (item == nullptr || proxy == nullptr ||
        qobject_cast<const QMenu *>(widget) == nullptr) {
      return false;
    }
    if (item->menuItemType == QStyleOptionMenuItem::Separator) {
      if (!item->text.isEmpty() || !item->icon.isNull()) {
        return false;
      }
      const qreal width = hairline(*painter);
      const QRectF bounds(item->rect);
      const qreal inset = metrics.px(8.0);
      if (bounds.width() > inset * 2.0) {
        painter->fillRect(QRectF(bounds.left() + inset,
                                bounds.center().y() - width * 0.5,
                                bounds.width() - inset * 2.0, width),
                          color(*item, QPalette::Mid));
      }
      return true;
    }
    if (item->menuItemType != QStyleOptionMenuItem::Normal &&
        item->menuItemType != QStyleOptionMenuItem::DefaultItem &&
        item->menuItemType != QStyleOptionMenuItem::SubMenu) {
      return false;
    }
    if (enabled(*item) && item->state.testFlag(QStyle::State_Selected)) {
      const QColor fill = color(*item, QPalette::Highlight);
      rounded_surface(*painter, item->rect, fill, fill, metrics.px(4.0));
    }

    // Keep Fusion's mnemonic, icon/check, submenu and RTL geometry. A
    // transparent native highlight leaves our rounded selection intact while
    // retaining selected icon modes and native foreground state handling.
    QStyleOptionMenuItem label(*item);
    if (label.menuHasCheckableItems) {
      label.maxIconWidth = std::max(
          label.maxIconWidth, metrics.px(kQtCheckSwitchWidth + 12));
    }
    const bool exclusive = item->checkType == QStyleOptionMenuItem::Exclusive;
    if (exclusive) {
      // Fusion derives its radio dot from the icon column. Keep a small choice
      // marker independent of the wider column required by boolean switches.
      label.checkType = item->icon.isNull() ? QStyleOptionMenuItem::NotCheckable
                                          : QStyleOptionMenuItem::NonExclusive;
    }
    label.palette.setCurrentColorGroup(color_group(*item));
    label.palette.setColor(QPalette::Highlight, QColor(0, 0, 0, 0));
    const qsizetype tab = label.text.indexOf(QLatin1Char('\t'));
    if (tab >= 0) {
      label.text.truncate(tab);
    }
    QStyleOptionMenuItem chrome(label);
    chrome.text.clear();
    proxy->QProxyStyle::drawControl(element, &chrome, painter, widget);

    // Keep native horizontal text layout and glyph geometry. Center only the
    // visible text ink; the font line box contains unused descent and leading.
    QStyleOptionMenuItem text(label);
    text.icon = QIcon{};
    text.checkType = QStyleOptionMenuItem::NotCheckable;
    text.checked = false;
    if (text.menuItemType == QStyleOptionMenuItem::SubMenu) {
      text.menuItemType = QStyleOptionMenuItem::Normal;
    }
    const auto draw_centered_text = [&](const QStyleOptionMenuItem &text_option,
                                        QStringView visible_text) {
      painter->save();
      painter->setClipRect(item->rect, Qt::IntersectClip);
      painter->translate(0.0,
                          centered_menu_text_offset(text_option, visible_text));
      proxy->QProxyStyle::drawControl(element, &text_option, painter, widget);
      painter->restore();
    };
    draw_centered_text(text, text.text);
    if (exclusive && item->icon.isNull() &&
        (item->checked || item->state.testFlag(QStyle::State_On) ||
         item->state.testFlag(QStyle::State_Sunken))) {
      const int extent = metrics.px(5);
      QRect column(item->rect.topLeft(),
                   QSize(std::max(label.maxIconWidth, metrics.px(26)),
                         item->rect.height()));
      column.translate(metrics.px(4), 0);
      const QRect visual_column =
          QStyle::visualRect(item->direction, item->rect, column);
      const QPointF center(QRectF(visual_column).center().x(),
                            QRectF(item->rect).center().y());
      const QRectF marker(center.x() - extent * 0.5,
                           center.y() - extent * 0.5, extent, extent);
      painter->save();
      painter->setRenderHint(QPainter::Antialiasing);
      painter->setPen(Qt::NoPen);
      painter->setBrush(color(*item, item->state.testFlag(QStyle::State_Selected)
                                        ? QPalette::HighlightedText
                                        : QPalette::WindowText));
      painter->drawEllipse(marker);
      painter->restore();
    }
    if (tab < 0) {
      return true;
    }

    // A separate native pass tints only the shortcut column, without copying
    // Fusion's private column constants or drawing icons and checks twice.
    QStyleOptionMenuItem shortcut(text);
    shortcut.text = item->text.mid(tab);
    shortcut.icon = QIcon{};
    shortcut.checkType = QStyleOptionMenuItem::NotCheckable;
    shortcut.checked = false;
    shortcut.menuItemType = QStyleOptionMenuItem::Normal;
    const QColor muted = color(*item, QPalette::PlaceholderText);
    shortcut.palette.setColor(QPalette::Text, muted);
    shortcut.palette.setColor(QPalette::HighlightedText, muted);
    draw_centered_text(shortcut, QStringView(item->text).mid(tab + 1));
    return true;
  }
  case QStyle::CE_PushButtonLabel: {
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    const auto *proxy = qobject_cast<const QProxyStyle *>(&style);
    if (button == nullptr || enabled(*button) || proxy == nullptr ||
        widget == nullptr ||
        !widget->property(kQtMutedDisabledButtonTextProperty).toBool()) {
      return false;
    }
    QStyleOptionButton label(*button);
    label.palette.setCurrentColorGroup(QPalette::Disabled);
    label.palette.setColor(QPalette::Disabled, QPalette::ButtonText,
                            qt_muted_disabled_button_text_color(button->palette, true));
    proxy->QProxyStyle::drawControl(element, &label, painter, widget);
    return true;
  }
  case QStyle::CE_PushButton: {
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    if (button == nullptr || !focused(*button) ||
        !button->state.testFlag(QStyle::State_KeyboardFocusChange) ||
        !(button->features.testFlag(QStyleOptionButton::DefaultButton) ||
          button->features.testFlag(QStyleOptionButton::Flat))) {
      return false;
    }
    // Fusion's complete button draw omits the focus primitive. Retain native
    // bevel, menu indicator, label and layout, then supply that missing cue for
    // buttons whose resting panel cannot distinguish keyboard focus.
    style.drawControl(QStyle::CE_PushButtonBevel, button, painter, widget);
    QStyleOptionButton label(*button);
    label.rect = style.subElementRect(QStyle::SE_PushButtonContents, button,
                                       widget);
    style.drawControl(QStyle::CE_PushButtonLabel, &label, painter, widget);
    QStyleOptionFocusRect focus;
    focus.QStyleOption::operator=(*button);
    focus.rect = style.subElementRect(QStyle::SE_PushButtonFocusRect, button,
                                       widget);
    style.drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, widget);
    return true;
  }
  case QStyle::CE_HeaderSection: {
    painter->fillRect(option->rect, surface_color(*option, false));
    const QRectF bounds(option->rect);
    painter->fillRect(QRectF(bounds.left(), bounds.bottom() - hairline(*painter),
                             bounds.width(), hairline(*painter)),
                       color(*option, QPalette::Dark));
    return true;
  }
  case QStyle::CE_TabBarTabShape: {
    const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
    if (tab == nullptr || tab->shape > QTabBar::RoundedEast) {
      return false;
    }
    const bool selected = tab->state.testFlag(QStyle::State_Selected);
    painter->save();
    QRectF bounds(tab->rect);
    // Normalize each native tab position to a horizontal strip. Qt still owns
    // label rotation, close-button geometry, hit testing and navigation.
    switch (tab->shape) {
    case QTabBar::RoundedSouth:
      painter->translate(bounds.left(), bounds.top() + bounds.height());
      painter->scale(1.0, -1.0);
      bounds.moveTopLeft(QPointF{});
      break;
    case QTabBar::RoundedWest:
      painter->translate(bounds.left(), bounds.top() + bounds.height());
      painter->rotate(-90.0);
      bounds = QRectF(0, 0, bounds.height(), bounds.width());
      break;
    case QTabBar::RoundedEast:
      painter->translate(bounds.left() + bounds.width(), bounds.top());
      painter->rotate(90.0);
      bounds = QRectF(0, 0, bounds.height(), bounds.width());
      break;
    default:
      break;
    }
    painter->fillRect(bounds, color(*tab, selected ? QPalette::AlternateBase
                                      : hovered(*tab) ? QPalette::Button
                                                      : QPalette::Window));
    const qreal edge = hairline(*painter);
    const qreal divider = tab->direction == Qt::RightToLeft
                              ? bounds.left() : bounds.right() - edge;
    painter->fillRect(QRectF(divider, bounds.top(), edge, bounds.height()),
                      color(*tab, QPalette::Mid));
    const qreal indicator = selected ? metrics.px(2.0) : edge;
    painter->fillRect(QRectF(bounds.left(), bounds.bottom() - indicator,
                             bounds.width(), indicator),
                      color(*tab, selected ? QPalette::Accent : QPalette::Mid));
    painter->restore();
    return true;
  }
  case QStyle::CE_ScrollBarSlider: {
    const bool horizontal = option->state.testFlag(QStyle::State_Horizontal);
    const int inset = std::max(1, metrics.px(3));
    const QRect bounds = horizontal
                             ? option->rect.adjusted(1, inset, -1, -inset)
                             : option->rect.adjusted(inset, 1, -inset, -1);
    const QColor fill = color(
        *option, enabled(*option) && option->state.testFlag(QStyle::State_Sunken)
                     ? QPalette::Accent
                     : hovered(*option) ? QPalette::Midlight : QPalette::Mid);
    rounded_surface(*painter, bounds, fill, fill, metrics.px(4.0));
    return true;
  }
  case QStyle::CE_ScrollBarAddPage:
  case QStyle::CE_ScrollBarSubPage:
    painter->fillRect(option->rect, color(*option, QPalette::Window));
    return true;
  default:
    return false;
  }
}

bool qt_draw_application_complex_control(
    QStyle::ComplexControl control, const QStyleOptionComplex *option,
    QPainter *painter, const QStyle &style, const QtUiScaleMetrics &metrics,
    const QWidget *widget) {
  if (painter == nullptr || !qt_application_style_active(option)) {
    return false;
  }
  if (control == QStyle::CC_Slider) {
    const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (slider == nullptr ||
        !(slider->subControls &
          (QStyle::SC_SliderGroove | QStyle::SC_SliderHandle))) {
      return false;
    }
    if (slider->subControls.testFlag(QStyle::SC_SliderTickmarks)) {
      // A tick-only request bypasses this painter and delegates to Fusion.
      QStyleOptionSlider ticks(*slider);
      ticks.subControls = QStyle::SC_SliderTickmarks;
      ticks.activeSubControls = QStyle::SC_None;
      ticks.state.setFlag(QStyle::State_HasFocus, false);
      style.drawComplexControl(control, &ticks, painter, widget);
    }
    draw_slider(*painter, *slider, style, metrics, widget);
    return true;
  }
  if (control == QStyle::CC_ScrollBar) {
    const auto *scroll = qstyleoption_cast<const QStyleOptionSlider *>(option);
    if (scroll == nullptr) {
      return false;
    }
    if (scroll->subControls.testFlag(QStyle::SC_ScrollBarGroove)) {
      painter->fillRect(scroll->rect, color(*scroll, QPalette::Window));
    }
    for (const auto sub_control : {QStyle::SC_ScrollBarSlider,
                                    QStyle::SC_ScrollBarSubLine,
                                    QStyle::SC_ScrollBarAddLine}) {
      if (!scroll->subControls.testFlag(sub_control)) {
        continue;
      }
      QStyleOptionSlider part(*scroll);
      part.rect = style.subControlRect(control, scroll, sub_control, widget);
      const bool active = scroll->activeSubControls.testFlag(sub_control);
      part.state.setFlag(QStyle::State_MouseOver, active && hovered(*scroll));
      part.state.setFlag(QStyle::State_Sunken,
                           active && scroll->state.testFlag(QStyle::State_Sunken));
      if (sub_control == QStyle::SC_ScrollBarSlider) {
        style.drawControl(QStyle::CE_ScrollBarSlider, &part, painter, widget);
        continue;
      }
      painter->fillRect(part.rect, color(part, QPalette::Window));
      if (hovered(part) || part.state.testFlag(QStyle::State_Sunken)) {
        draw_surface(*painter, part, metrics, false);
      }
      const bool previous = sub_control == QStyle::SC_ScrollBarSubLine;
      QStyle::PrimitiveElement arrow = previous ? QStyle::PE_IndicatorArrowUp
                                                : QStyle::PE_IndicatorArrowDown;
      if (scroll->orientation == Qt::Horizontal) {
        const bool left = previous == (scroll->direction == Qt::LeftToRight);
        arrow = left ? QStyle::PE_IndicatorArrowLeft
                     : QStyle::PE_IndicatorArrowRight;
      }
      draw_arrow(arrow, part, part.rect, *painter, style, metrics, widget);
    }
    return true;
  }
  if (control == QStyle::CC_ComboBox) {
    const auto *combo = qstyleoption_cast<const QStyleOptionComboBox *>(option);
    if (combo == nullptr) {
      return false;
    }
    if (combo->frame && combo->subControls.testFlag(QStyle::SC_ComboBoxFrame)) {
      draw_surface(*painter, *combo, metrics, combo->editable, false, widget);
    }
    if (combo->subControls.testFlag(QStyle::SC_ComboBoxArrow)) {
      const QRect arrow = style.subControlRect(control, option,
                                                QStyle::SC_ComboBoxArrow, widget);
      draw_arrow(QStyle::PE_IndicatorArrowDown, *combo, arrow, *painter,
                   style, metrics, widget);
    }
    return true;
  }
  if (control != QStyle::CC_SpinBox) {
    return false;
  }
  const auto *spin = qstyleoption_cast<const QStyleOptionSpinBox *>(option);
  if (spin == nullptr) {
    return false;
  }
  if (spin->frame && spin->subControls.testFlag(QStyle::SC_SpinBoxFrame)) {
    draw_surface(*painter, *spin, metrics, true, false, widget);
  }
  if (spin->buttonSymbols == QAbstractSpinBox::NoButtons) {
    return true;
  }
  for (const bool up : {true, false}) {
    const auto sub_control = up ? QStyle::SC_SpinBoxUp : QStyle::SC_SpinBoxDown;
    if (!spin->subControls.testFlag(sub_control)) {
      continue;
    }
    QStyleOption button(*spin);
    button.rect = style.subControlRect(control, option, sub_control, widget);
    const bool step_enabled = spin->stepEnabled.testFlag(
        up ? QAbstractSpinBox::StepUpEnabled : QAbstractSpinBox::StepDownEnabled);
    button.state.setFlag(QStyle::State_Enabled, enabled(*spin) && step_enabled);
    const bool active = spin->activeSubControls.testFlag(sub_control);
    button.state.setFlag(QStyle::State_MouseOver, active && hovered(*spin));
    button.state.setFlag(QStyle::State_Sunken,
                           active && spin->state.testFlag(QStyle::State_Sunken));
    button.state.setFlag(QStyle::State_HasFocus, false);
    if (hovered(button) || button.state.testFlag(QStyle::State_Sunken)) {
      draw_surface(*painter, button, metrics, false);
    }
    const auto glyph = spin->buttonSymbols == QAbstractSpinBox::PlusMinus
                           ? (up ? QStyle::PE_IndicatorSpinPlus
                                 : QStyle::PE_IndicatorSpinMinus)
                           : (up ? QStyle::PE_IndicatorArrowUp
                                 : QStyle::PE_IndicatorArrowDown);
    draw_arrow(glyph, button, button.rect, *painter, style, metrics, widget);
  }
  return true;
}

} // namespace quader
