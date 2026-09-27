////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_application_style.hpp"

#include "ui/qt_ui_scale_metrics.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleOption>
#include <QTabBar>
#include <QToolBar>
#include <QToolButton>
#include <QVariant>

#include <algorithm>

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
  painter.drawRoundedRect(bounds, usable_radius, usable_radius);
  painter.restore();
}

void draw_surface(QPainter &painter, const QStyleOption &option,
                  const QtUiScaleMetrics &metrics, bool field,
                  bool default_button = false) {
  QColor fill = surface_color(option, field);
  QColor border = field ? color(option, QPalette::Midlight)
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
                  metrics.px(4.0));
}

void draw_indicator(QPainter &painter, const QStyleOption &option,
                    const QtUiScaleMetrics &metrics, bool radio) {
  const bool checked = option.state.testFlag(QStyle::State_On);
  const bool partial = option.state.testFlag(QStyle::State_NoChange);
  QColor border = color(option, QPalette::Midlight);
  if (focused(option) || hovered(option) || checked || partial) {
    border = color(option, QPalette::Accent);
  }
  const QColor fill = surface_color(option, !checked && !partial);
  rounded_surface(painter, option.rect, fill, border,
                  radio ? option.rect.height() * 0.5 : metrics.px(3.0));
  if (!checked && !partial) {
    return;
  }
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF bounds(option.rect);
  const QColor mark = color(option, QPalette::Accent);
  if (radio) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(mark);
    painter.drawEllipse(bounds.adjusted(bounds.width() * 0.29,
                                        bounds.height() * 0.29,
                                        -bounds.width() * 0.29,
                                        -bounds.height() * 0.29));
  } else {
    QPen pen(mark, std::max<qreal>(hairline(painter), metrics.px(1.6)));
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QPainterPath path;
    if (partial) {
      path.moveTo(bounds.left() + bounds.width() * 0.27, bounds.center().y());
      path.lineTo(bounds.right() - bounds.width() * 0.27, bounds.center().y());
    } else {
      path.moveTo(bounds.left() + bounds.width() * 0.24,
                   bounds.top() + bounds.height() * 0.51);
      path.lineTo(bounds.left() + bounds.width() * 0.43,
                   bounds.top() + bounds.height() * 0.70);
      path.lineTo(bounds.left() + bounds.width() * 0.77,
                   bounds.top() + bounds.height() * 0.32);
    }
    painter.drawPath(path);
  }
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
  case QStyle::PE_PanelButtonCommand:
  case QStyle::PE_PanelButtonTool: {
    const auto *button = qstyleoption_cast<const QStyleOptionButton *>(option);
    const auto *tool_button = qobject_cast<const QToolButton *>(widget);
    const bool toolbar_button =
        tool_button != nullptr &&
        qobject_cast<const QToolBar *>(widget->parentWidget()) != nullptr;
    QStyleOption face = menu_glyph_option(*option, widget);
    if (tool_button != nullptr && tool_button->isCheckable() &&
        !face.state.testFlag(QStyle::State_KeyboardFocusChange)) {
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
      if (element == QStyle::PE_PanelLineEdit) {
        painter->fillRect(option->rect, surface_color(*option, true));
      }
      return true;
    }
    draw_surface(*painter, *option, metrics, true);
    return true;
  }
  case QStyle::PE_IndicatorCheckBox:
    draw_indicator(*painter, menu_glyph_option(*option, widget), metrics, false);
    return true;
  case QStyle::PE_IndicatorRadioButton:
    draw_indicator(*painter, menu_glyph_option(*option, widget), metrics, true);
    return true;
  case QStyle::PE_IndicatorTabClose: {
    const bool pressed = enabled(*option) &&
                         option->state.testFlag(QStyle::State_Sunken);
    if (hovered(*option) || pressed || focused(*option)) {
      draw_surface(*painter, *option, metrics, false);
    }
    const bool emphasized = enabled(*option) &&
                            (hovered(*option) || pressed ||
                             option->state.testFlag(QStyle::State_Selected));
    const qreal extent = std::min<qreal>(
        metrics.px(8.0),
        std::min(option->rect.width(), option->rect.height()) * 0.5);
    const QPointF center = QRectF(option->rect).center();
    const QRectF glyph(center.x() - extent * 0.5,
                       center.y() - extent * 0.5, extent, extent);
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    QPen pen(color(*option, emphasized ? QPalette::Text : QPalette::WindowText),
             std::max<qreal>(hairline(*painter), metrics.px(1.4)));
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
  if (painter == nullptr || !qt_application_style_active(option)) {
    return false;
  }
  switch (element) {
  case QStyle::CE_DockWidgetTitle: {
    const auto *dock = qstyleoption_cast<const QStyleOptionDockWidget *>(option);
    if (dock == nullptr) {
      return false;
    }
    painter->save();
    painter->fillRect(dock->rect, color(*dock, QPalette::Button));
    const QRectF bounds(dock->rect);
    const qreal edge = hairline(*painter);
    const QRectF divider = dock->verticalTitleBar
                              ? QRectF(bounds.right() - edge, bounds.top(),
                                       edge, bounds.height())
                              : QRectF(bounds.left(), bounds.bottom() - edge,
                                       bounds.width(), edge);
    painter->fillRect(divider, color(*dock, QPalette::Midlight));

    // Keep Qt's title allocation, including button space and RTL placement.
    // Styling the title subcontrol with QSS bypasses its foreground/font rules.
    QRect text_rect = style.subElementRect(
        QStyle::SE_DockWidgetTitleBarText, dock, widget);
    if (dock->verticalTitleBar) {
      painter->translate(text_rect.left(), text_rect.bottom() + 1);
      painter->rotate(-90);
      text_rect = QRect(0, 0, text_rect.height(), text_rect.width());
    }
    text_rect.adjust(metrics.px(2), 0, -metrics.px(2), 0);
    // QDockWidget seeds its painter with a platform title font that does not
    // follow our application scale controller. Use the scaled widget font.
    QFont title_font = widget != nullptr ? widget->font() : painter->font();
    title_font.setWeight(QFont::DemiBold);
    painter->setFont(title_font);
    painter->setPen(color(*dock, QPalette::Text));
    painter->setClipRect(text_rect, Qt::IntersectClip);
    const QString title = QFontMetrics(title_font).elidedText(
        dock->title, Qt::ElideRight, std::max(0, text_rect.width()));
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
    label.palette.setCurrentColorGroup(color_group(*item));
    label.palette.setColor(QPalette::Highlight, QColor(0, 0, 0, 0));
    const qsizetype tab = label.text.indexOf(QLatin1Char('\t'));
    if (tab < 0) {
      proxy->QProxyStyle::drawControl(element, &label, painter, widget);
      return true;
    }
    label.text.truncate(tab);
    proxy->QProxyStyle::drawControl(element, &label, painter, widget);

    // A separate native pass tints only the shortcut column, without copying
    // Fusion's private column constants or drawing icons and checks twice.
    QStyleOptionMenuItem shortcut(label);
    shortcut.text = item->text.mid(tab);
    shortcut.icon = QIcon{};
    shortcut.checkType = QStyleOptionMenuItem::NotCheckable;
    shortcut.checked = false;
    shortcut.menuItemType = QStyleOptionMenuItem::Normal;
    const QColor muted = color(*item, QPalette::PlaceholderText);
    shortcut.palette.setColor(QPalette::Text, muted);
    shortcut.palette.setColor(QPalette::HighlightedText, muted);
    proxy->QProxyStyle::drawControl(element, &shortcut, painter, widget);
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
    const bool selected = option->state.testFlag(QStyle::State_Selected);
    const bool connected_surface =
        tab->shape == QTabBar::RoundedNorth && widget != nullptr &&
        widget->property(kQtConnectedTabSurfaceProperty).toBool();
    const QColor fill =
        selected && connected_surface
            ? color(*option, QPalette::AlternateBase)
            : selected || hovered(*option)
                  ? surface_color(*option, false)
                  : color(*option, QPalette::Window);
    QRect surface = option->rect;
    if (selected && connected_surface) {
      // Native tab geometry may reserve a base overlap. Fill through that
      // space so the selected tab joins the shared content surface below.
      surface.setBottom(widget->rect().bottom());
    }
    painter->fillRect(surface, fill);
    if (selected && !connected_surface) {
      const QRectF bounds(option->rect);
      const qreal width = std::max(hairline(*painter), metrics.px(2.0));
      QRectF stripe;
      switch (tab->shape) {
      case QTabBar::RoundedSouth:
        stripe = {bounds.left(), bounds.top(), bounds.width(), width};
        break;
      case QTabBar::RoundedWest:
        stripe = {bounds.right() - width, bounds.top(), width, bounds.height()};
        break;
      case QTabBar::RoundedEast:
        stripe = {bounds.left(), bounds.top(), width, bounds.height()};
        break;
      default:
        stripe = {bounds.left(), bounds.bottom() - width, bounds.width(), width};
        break;
      }
      painter->fillRect(stripe, color(*option, QPalette::Accent));
    }
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
      draw_surface(*painter, *combo, metrics, combo->editable);
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
    draw_surface(*painter, *spin, metrics, true);
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
