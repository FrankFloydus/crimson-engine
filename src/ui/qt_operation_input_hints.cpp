////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_operation_input_hints.hpp"

#include "api/quader_commands_api.hpp"
#include "api/quader_tools_api.hpp"
#include "core/localization_service.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace quader {
namespace {

QFont hint_font(const QWidget &widget) {
  QFont font = widget.font();
  if (font.pointSizeF() > 0) {
    font.setPointSizeF(font.pointSizeF() * 0.85);
  } else {
    font.setPixelSize(std::max(1, static_cast<int>(font.pixelSize() * 0.85)));
  }
  return font;
}

} // namespace

QtOperationInputHints::QtOperationInputHints(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("operationInputHints"));
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  hide();
}

void QtOperationInputHints::refresh(const QuaderToolsApi &tools,
                                   const QuaderCommandsApi &commands,
                                   const LocalizationService &localization) {
  const Stamp stamp{&tools, &commands, &localization,
                    tools.active_input_hints_revision(), commands.keybindings_revision(),
                    localization.revision()};
  if (!presenter_.needs_refresh(stamp)) {
    return;
  }
  std::vector<QtOperationInputHint> hints;
  for (const auto &hint : tools.active_input_hints()) {
    QString badge;
    switch (hint.gesture) {
    case QuaderOperationInputGesture::Command: {
      const auto command = commands.descriptor(hint.command);
      if (!command || !command->enabled || command->shortcut.empty()) {
        continue;
      }
      badge = QString::fromStdString(command->shortcut);
      break;
    }
    case QuaderOperationInputGesture::PrimaryClick:
      badge = QString::fromStdString(localization.text(
          "operation.hints.key.primary_click", "Ctrl + Click"));
      break;
    case QuaderOperationInputGesture::Wheel:
      badge = QString::fromStdString(localization.text(
          "operation.hints.key.wheel", "Wheel +/-"));
      break;
    }
    hints.push_back({std::move(badge), QString::fromStdString(
        localization.text(hint.label_key, hint.label_fallback))});
  }
  ++projection_revision_;
  if (hints_ != hints) {
    hints_ = std::move(hints);
    QStringList description;
    for (const auto &hint : hints_) {
      description.push_back(hint.badge + QStringLiteral(": ") + hint.label);
    }
    const QString text = description.join(QStringLiteral("; "));
    setAccessibleName(text);
    setToolTip(text);
    setVisible(!hints_.empty());
    refresh_geometry();
  }
  presenter_.mark_presented(stamp);
}

void QtOperationInputHints::invalidate() {
  presenter_.invalidate();
  hints_.clear();
  geometry_.clear();
  setAccessibleName({});
  setToolTip({});
  hide();
  refresh_geometry();
}

void QtOperationInputHints::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.factor() == metrics.factor()) {
    return;
  }
  metrics_ = metrics;
  refresh_geometry();
}

int QtOperationInputHints::layout_items(int width, std::vector<ItemGeometry> *items) const {
  if (hints_.empty()) {
    return 0;
  }
  const QFontMetrics fm(hint_font(*this));
  const int gap = metrics_.px(4);
  const int item_gap = metrics_.px(12);
  const int badge_padding = metrics_.px(3);
  const int badge_height = fm.height() + metrics_.px(2);
  const int row_height = std::max(fontMetrics().height(), badge_height) + metrics_.px(4);
  const int y = (row_height - badge_height) / 2;
  const int available = std::max(0, width);
  int x = 0;
  for (const auto &hint : hints_) {
    const int badge_width = fm.horizontalAdvance(hint.badge) + badge_padding * 2;
    const int label_width = fm.horizontalAdvance(hint.label);
    const int item_width = badge_width + gap + label_width;
    if (x >= available) {
      break;
    }
    if (items) {
      const int badge_extent = std::min(badge_width, available - x);
      items->push_back({QRect(x, y, badge_extent, badge_height),
                        QRect(x + badge_extent + gap, y,
                              std::max(0, std::min(label_width,
                                  available - x - badge_extent - gap)), badge_height)});
    }
    x += item_width + item_gap;
  }
  return row_height;
}

QSize QtOperationInputHints::sizeHint() const {
  const QFontMetrics fm(hint_font(*this));
  int width = 0;
  for (const auto &hint : hints_) {
    width += fm.horizontalAdvance(hint.badge) + fm.horizontalAdvance(hint.label) +
             metrics_.px(22);
  }
  return {std::max(0, width - metrics_.px(12)),
          layout_items(0, nullptr)};
}

QSize QtOperationInputHints::minimumSizeHint() const { return {0, sizeHint().height()}; }

void QtOperationInputHints::refresh_geometry() {
  geometry_.clear();
  const int height = layout_items(width(), &geometry_);
  if (minimumHeight() != height || maximumHeight() != height) {
    setFixedHeight(height);
  }
  updateGeometry();
  update();
}

void QtOperationInputHints::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (event->oldSize().width() != event->size().width()) {
    refresh_geometry();
  }
}

void QtOperationInputHints::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
    refresh_geometry();
  }
}

void QtOperationInputHints::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setFont(hint_font(*this));
  const QFontMetrics fm(painter.font());
  const int padding = metrics_.px(3);
  QColor outline = palette().color(QPalette::WindowText);
  outline.setAlpha(180);
  QPen outline_pen(outline);
  outline_pen.setCosmetic(true);
  for (std::size_t i = 0; i < geometry_.size(); ++i) {
    const auto &item = geometry_[i];
    const auto &hint = hints_[i];
    painter.setPen(outline_pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(item.badge).adjusted(0.5, 0.5, -0.5, -0.5),
                            metrics_.px(3), metrics_.px(3));
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(item.badge.adjusted(padding, 0, -padding, 0), Qt::AlignCenter,
                     fm.elidedText(hint.badge, Qt::ElideRight,
                                   std::max(0, item.badge.width() - padding * 2)));
    painter.setPen(palette().color(QPalette::WindowText));
    painter.drawText(item.label, Qt::AlignVCenter | Qt::AlignLeft,
                     fm.elidedText(hint.label, Qt::ElideRight, item.label.width()));
  }
}

} // namespace quader
