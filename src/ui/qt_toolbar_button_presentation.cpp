////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_toolbar_button_presentation.hpp"

#include <QAction>
#include <QColor>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleOption>
#include <QToolBar>
#include <QToolButton>

namespace quader {
namespace {

QColor blend(const QColor &from, const QColor &to, qreal amount) {
  return QColor::fromRgbF(
      from.redF() + ((to.redF() - from.redF()) * amount),
      from.greenF() + ((to.greenF() - from.greenF()) * amount),
      from.blueF() + ((to.blueF() - from.blueF()) * amount),
      from.alphaF() + ((to.alphaF() - from.alphaF()) * amount));
}

/**
 * Adapts only the semantic face state; the stock Fusion base style still draws
 * the complete button, label, focus rectangle, disabled state, and press.
 */
class QtToolbarButtonPresentationStyle final : public QProxyStyle {
public:
  explicit QtToolbarButtonPresentationStyle(QObject *parent)
      : QProxyStyle(QStringLiteral("fusion")) {
    setObjectName(QStringLiteral("quaderToolbarButtonPresentation"));
    setParent(parent);
  }

  void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                     QPainter *painter,
                     const QWidget *widget = nullptr) const override {
    if (element != PE_PanelButtonCommand || option == nullptr ||
        painter == nullptr) {
      QProxyStyle::drawPrimitive(element, option, painter, widget);
      return;
    }

    QStyleOption face = *option;
    const bool selected = face.state.testFlag(State_On);
    const bool hovered = face.state.testFlag(State_MouseOver);
    const bool physically_pressed = face.state.testFlag(State_Sunken);
    const bool keyboard_focused = face.state.testFlag(State_HasFocus) &&
                                  face.state.testFlag(State_KeyboardFocusChange);
    const QPalette::ColorGroup color_group =
        qt_toolbar_button_color_group(face.state);
    const QColor accent =
        face.palette.color(color_group, QPalette::Highlight);

    if (selected) {
      constexpr qreal kSelectedAccentMix = 0.30;
      constexpr qreal kSelectedHoverAccentMix = 0.40;
      face.palette.setColor(
          QPalette::Button,
          blend(face.palette.color(color_group, QPalette::Button), accent,
                hovered ? kSelectedHoverAccentMix : kSelectedAccentMix));
      face.state.setFlag(State_On, false);
      face.state.setFlag(State_Raised, !physically_pressed);
    } else if (hovered && face.state.testFlag(State_Enabled) &&
               !physically_pressed) {
      constexpr qreal kHoverHighlightMix = 0.22;
      face.palette.setColor(
          QPalette::Button,
          blend(face.palette.color(color_group, QPalette::Button), accent,
                kHoverHighlightMix));
    }

    QProxyStyle::drawPrimitive(element, &face, painter, widget);

    if (!selected && !keyboard_focused) {
      return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    // Fusion's generic focus wash is deliberately subtle. Toolbar actions use
    // a full-opacity palette accent so keyboard focus stays unambiguous.
    QPen outline(accent, keyboard_focused ? 2.0 : 1.0);
    outline.setJoinStyle(Qt::RoundJoin);
    painter->setPen(outline);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(QRectF(option->rect).adjusted(1.5, 1.5, -1.5,
                                                             -1.5),
                             1.0, 1.0);
    painter->restore();
  }
};

void synchronize_accessibility(QToolButton &button, const QAction &action) {
  button.setAccessibleName(action.text());
  button.setAccessibleDescription(action.toolTip());
}

} // namespace

QPalette::ColorGroup qt_toolbar_button_color_group(QStyle::State state) {
  if (!state.testFlag(QStyle::State_Enabled)) {
    return QPalette::Disabled;
  }
  return state.testFlag(QStyle::State_Active) ? QPalette::Active
                                               : QPalette::Inactive;
}

QStyle &install_native_toolbar_button_presentation(QToolBar &toolbar) {
  return *new QtToolbarButtonPresentationStyle(&toolbar);
}

void configure_native_toolbar_button_face(QToolBar &toolbar, QAction &action,
                                          QStyle &presentation) {
  auto *button =
      qobject_cast<QToolButton *>(toolbar.widgetForAction(&action));
  if (button == nullptr) {
    return;
  }

  button->setStyle(&presentation);
  button->setAutoRaise(false);
  button->setAttribute(Qt::WA_Hover, true);
  button->setMouseTracking(true);
  button->setFocusPolicy(Qt::StrongFocus);
  button->setMinimumSize(40, 40);
  synchronize_accessibility(*button, action);
  QObject::connect(&action, &QAction::changed, button, [button, &action]() {
    synchronize_accessibility(*button, action);
    button->update();
  });
}

} // namespace quader
