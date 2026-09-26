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
#include <QStyle>

#include <algorithm>
#include <array>
#include <numeric>
#include <utility>

namespace quader {
namespace {

QString modifier_binding(QString shortcut) {
  const auto separator = shortcut.lastIndexOf(QLatin1Char('+'));
  if (separator <= 0) {
    return shortcut;
  }
  const auto pointer = shortcut.sliced(separator + 1);
  if (!pointer.contains(QStringLiteral("Mouse")) &&
      !pointer.startsWith(QStringLiteral("Wheel"))) {
    return shortcut;
  }
  const auto modifiers = shortcut.first(separator).split(QLatin1Char('+'));
  for (const auto &modifier : modifiers) {
    if (modifier != QStringLiteral("Ctrl") && modifier != QStringLiteral("Shift") &&
        modifier != QStringLiteral("Alt") && modifier != QStringLiteral("Meta")) {
      return shortcut;
    }
  }
  return modifiers.join(QStringLiteral(" + "));
}

QString friendly_binding(QString shortcut, const LocalizationService &localization) {
  const auto localized = [&](const char *key, const char *fallback) {
    return QString::fromStdString(localization.text(key, fallback));
  };
  const std::array<std::pair<QString, QString>, 14> names{{
      {QStringLiteral("LeftMouseDoubleClick"), localized("operation.hints.key.primary_double_click", "Double LMB")},
      {QStringLiteral("RightMouseDoubleClick"), localized("operation.hints.key.right_double_click", "Double RMB")},
      {QStringLiteral("MiddleMouseDoubleClick"), localized("operation.hints.key.middle_double_click", "Double MMB")},
      {QStringLiteral("LeftMouseClick"), localized("operation.hints.key.primary_click", "LMB")},
      {QStringLiteral("RightMouseClick"), localized("operation.hints.key.right_click", "RMB")},
      {QStringLiteral("MiddleMouseClick"), localized("operation.hints.key.middle_click", "MMB")},
      {QStringLiteral("LeftMouseDrag"), localized("operation.hints.key.primary_drag", "LMB Drag")},
      {QStringLiteral("RightMouseDrag"), localized("operation.hints.key.right_drag", "RMB Drag")},
      {QStringLiteral("MiddleMouseDrag"), localized("operation.hints.key.middle_drag", "MMB Drag")},
      {QStringLiteral("LMouse"), QStringLiteral("LMB")},
      {QStringLiteral("RMouse"), QStringLiteral("RMB")},
      {QStringLiteral("MMouse"), QStringLiteral("MMB")},
      {QStringLiteral("WheelUp"), localized("operation.hints.key.wheel_up", "Wheel Up")},
      {QStringLiteral("WheelDown"), localized("operation.hints.key.wheel_down", "Wheel Down")},
  }};
  for (const auto &[canonical, friendly] : names) {
    if (shortcut.contains(canonical)) {
      shortcut.replace(canonical, friendly);
      shortcut.replace(QStringLiteral("+"), QStringLiteral(" + "));
      break;
    }
  }
  return shortcut;
}

QString grouped_bindings(const QuaderOperationInputHint &hint,
                         const QuaderCommandsApi &commands,
                         const LocalizationService &localization) {
  QStringList bindings;
  const auto append = [&](QuaderCommandId id) {
    QString binding;
    if (hint.gesture == QuaderOperationInputGesture::Command) {
      const auto command = commands.descriptor(id);
      if (command && command->enabled) {
        binding = QString::fromStdString(command->shortcut);
      }
    } else {
      binding = QString::fromStdString(commands.shortcut_for(id));
    }
    if (hint.gesture == QuaderOperationInputGesture::KeybindingModifier) {
      binding = modifier_binding(std::move(binding));
    }
    if (!binding.isEmpty() && !bindings.contains(binding)) {
      bindings.push_back(std::move(binding));
    }
  };
  append(hint.command);
  const auto count = std::min<std::size_t>(hint.related_command_count,
                                          hint.related_commands.size());
  for (std::size_t index = 0; index < count; ++index) {
    append(hint.related_commands[index]);
  }
  if (bindings.size() == 2) {
    QString upward = bindings[0];
    QString downward = bindings[1];
    if (downward.endsWith(QStringLiteral("WheelUp"))) {
      std::swap(upward, downward);
    }
    if (upward.endsWith(QStringLiteral("WheelUp")) &&
        downward.endsWith(QStringLiteral("WheelDown")) &&
        upward.chopped(7) == downward.chopped(9)) {
      return upward.chopped(7).replace(QStringLiteral("+"), QStringLiteral(" + ")) +
             QString::fromStdString(localization.text("operation.hints.key.wheel", "Wheel"));
    }
  }
  bool compact = !bindings.empty();
  for (auto &binding : bindings) {
    binding = friendly_binding(std::move(binding), localization);
    compact = compact && binding.size() == 1;
  }
  return bindings.join(compact ? QStringLiteral("/") : QStringLiteral(" / "));
}

QString intrinsic_badge(QuaderOperationInputGesture gesture,
                        const LocalizationService &localization) {
  using Gesture = QuaderOperationInputGesture;
  const auto localized = [&](const char *key, const char *fallback) {
    return QString::fromStdString(localization.text(key, fallback));
  };
  switch (gesture) {
  case Gesture::PrimaryClick: return localized("operation.hints.key.primary_click", "LMB");
  case Gesture::ShiftPrimaryClick: return localized("operation.hints.key.shift_primary_click", "Shift + LMB");
  case Gesture::ControlPrimaryClick: return localized("operation.hints.key.control_primary_click", "Ctrl + LMB");
  case Gesture::Wheel: return localized("operation.hints.key.wheel", "Wheel");
  case Gesture::PrimaryDrag: return localized("operation.hints.key.primary_drag", "LMB Drag");
  case Gesture::ShiftPrimaryDrag: return localized("operation.hints.key.shift_primary_drag", "Shift + LMB Drag");
  case Gesture::ControlPrimaryDrag: return localized("operation.hints.key.control_primary_drag", "Ctrl + LMB Drag");
  case Gesture::ShiftAltPrimaryDrag: return localized("operation.hints.key.shift_alt_primary_drag", "Shift / Alt + LMB Drag");
  case Gesture::Shift: return localized("operation.hints.key.shift", "Shift");
  case Gesture::Control: return localized("operation.hints.key.control", "Ctrl");
  case Gesture::Alt: return localized("operation.hints.key.alt", "Alt");
  case Gesture::ControlShift: return localized("operation.hints.key.control_shift", "Ctrl + Shift");
  case Gesture::MiddleClick: return localized("operation.hints.key.middle_click", "MMB");
  case Gesture::RightClick: return localized("operation.hints.key.right_click", "RMB");
  case Gesture::Escape: return localized("operation.hints.key.escape", "Esc");
  case Gesture::Enter: return localized("operation.hints.key.enter", "Enter");
  case Gesture::EscapeOrRightClick: return localized("operation.hints.key.escape_right_click", "Esc / RMB");
  case Gesture::MiddleDrag: return localized("operation.hints.key.middle_drag", "MMB Drag");
  case Gesture::ShiftMiddleDrag: return localized("operation.hints.key.shift_middle_drag", "Shift + MMB Drag");
  case Gesture::ControlMiddleDrag: return localized("operation.hints.key.control_middle_drag", "Ctrl + MMB Drag");
  case Gesture::MiddleDoubleClick: return localized("operation.hints.key.middle_double_click", "Double MMB");
  case Gesture::PrimaryDoubleClick: return localized("operation.hints.key.primary_double_click", "Double LMB");
  case Gesture::ControlWheel: return localized("operation.hints.key.control_wheel", "Ctrl + Wheel");
  case Gesture::ShiftWheel: return localized("operation.hints.key.shift_wheel", "Shift + Wheel");
  case Gesture::PrimaryOrMiddleDrag: return localized("operation.hints.key.primary_middle_drag", "LMB / MMB Drag");
  case Gesture::PrimaryOrMiddleDoubleClick: return localized("operation.hints.key.primary_middle_double_click", "Double LMB / MMB");
  case Gesture::Information:
  case Gesture::Command:
  case Gesture::Keybinding:
  case Gesture::KeybindingHold:
  case Gesture::KeybindingModifier: return {};
  }
  return {};
}

} // namespace

QtOperationInputHints::QtOperationInputHints(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("operationInputHints"));
  setFocusPolicy(Qt::NoFocus);
  setAttribute(Qt::WA_TransparentForMouseEvents);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  refresh_geometry();
  hide();
}

void QtOperationInputHints::refresh(const QuaderToolsApi &tools,
                                   const QuaderCommandsApi &commands,
                                   const LocalizationService &localization,
                                   const QuaderOperationInputContext &context) {
  const Stamp stamp{&tools, &commands, &localization, nullptr,
                    tools.active_input_hints_revision(context), commands.keybindings_revision(),
                    localization.revision(), context};
  if (presenter_.needs_refresh(stamp)) {
    present(tools.active_input_hints(context), stamp, commands, localization);
  }
}

void QtOperationInputHints::refresh_snapshot(
    std::span<const QuaderOperationInputHint> hints, std::uint64_t revision,
    const void *source_owner, const QuaderCommandsApi &commands,
    const LocalizationService &localization) {
  const Stamp stamp{nullptr, &commands, &localization, source_owner, revision,
                    commands.keybindings_revision(), localization.revision(), {}};
  if (presenter_.needs_refresh(stamp)) {
    present(hints, stamp, commands, localization);
  }
}

void QtOperationInputHints::present(std::span<const QuaderOperationInputHint> source,
                                   const Stamp &stamp,
                                   const QuaderCommandsApi &commands,
                                   const LocalizationService &localization) {
  std::vector<QtOperationInputHint> hints;
  hints.reserve(source.size());
  for (const auto &hint : source) {
    const bool bound = hint.gesture == QuaderOperationInputGesture::Command ||
                       hint.gesture == QuaderOperationInputGesture::Keybinding ||
                       hint.gesture == QuaderOperationInputGesture::KeybindingHold ||
                       hint.gesture == QuaderOperationInputGesture::KeybindingModifier;
    QString badge = bound ? grouped_bindings(hint, commands, localization)
                          : intrinsic_badge(hint.gesture, localization);
    if (badge.isEmpty() && hint.gesture != QuaderOperationInputGesture::Information) {
      continue;
    }
    if (hint.gesture == QuaderOperationInputGesture::KeybindingHold) {
      badge = QString::fromStdString(localization.text("operation.hints.key.held", "%1 held"))
                  .arg(badge);
    }
    QString label = QString::fromStdString(localization.text(hint.label_key, hint.label_fallback));
    if (!hint.detail.empty()) {
      label += QStringLiteral(" ") + QString::fromStdString(hint.detail);
    }
    hints.push_back({std::move(badge), std::move(label), hint.priority, hint.active});
  }
  ++projection_revision_;
  if (hints_ != hints) {
    hints_ = std::move(hints);
    QStringList description;
    for (const auto &hint : hints_) {
      description.push_back(hint.badge.isEmpty() ? hint.label
                                                : hint.badge + QStringLiteral(": ") + hint.label);
    }
    const QString text = description.join(QStringLiteral("; "));
    setAccessibleName(text);
    setToolTip(text);
    refresh_geometry();
    setVisible(!hints_.empty());
  }
  presenter_.mark_presented(stamp);
}

void QtOperationInputHints::invalidate() {
  presenter_.invalidate();
  hints_.clear();
  geometry_.clear();
  visible_hint_indices_.clear();
  setAccessibleName({});
  setToolTip({});
  hide();
  refresh_geometry();
}

void QtOperationInputHints::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.factor() != metrics.factor()) {
    metrics_ = metrics;
    refresh_geometry();
  }
}

int QtOperationInputHints::row_height() const {
  // QStatusBar uses hidden children's minima for its row strut. Keep that
  // reservation stable while content changes, including before the first hint.
  return fontMetrics().height() + metrics_.px(6);
}

int QtOperationInputHints::item_width(const QtOperationInputHint &hint) const {
  const QFontMetrics fm(font());
  return fm.horizontalAdvance(hint.label) + (hint.badge.isEmpty() ? 0 :
      fm.horizontalAdvance(hint.badge) + metrics_.px(6) + metrics_.px(4));
}

QSize QtOperationInputHints::sizeHint() const {
  int width = 0;
  for (const auto &hint : hints_) {
    width += item_width(hint) + metrics_.px(12);
  }
  return {std::max(0, width - metrics_.px(12)), row_height()};
}

QSize QtOperationInputHints::minimumSizeHint() const { return {0, row_height()}; }

void QtOperationInputHints::refresh_geometry() {
  geometry_.clear();
  visible_hint_indices_.clear();
  std::vector<std::size_t> candidates(hints_.size());
  std::iota(candidates.begin(), candidates.end(), 0);
  std::stable_sort(candidates.begin(), candidates.end(), [&](auto left, auto right) {
    const auto score = [&](auto index) {
      return static_cast<int>(hints_[index].priority) + (hints_[index].active ? 256 : 0);
    };
    return score(left) > score(right);
  });
  int occupied = 0;
  for (const auto index : candidates) {
    const int extent = item_width(hints_[index]) +
                       (visible_hint_indices_.empty() ? 0 : metrics_.px(12));
    if (extent <= width() - occupied) {
      visible_hint_indices_.push_back(index);
      occupied += extent;
    }
  }
  std::ranges::sort(visible_hint_indices_);
  const int height = row_height();
  const int badge_height = fontMetrics().height() + metrics_.px(2);
  const int y = (height - badge_height) / 2;
  int x = 0;
  const QRect row(0, 0, width(), height);
  for (const auto index : visible_hint_indices_) {
    const auto &hint = hints_[index];
    const int badge_width = hint.badge.isEmpty() ? 0 :
        fontMetrics().horizontalAdvance(hint.badge) + metrics_.px(6);
    const int gap = hint.badge.isEmpty() ? 0 : metrics_.px(4);
    const int label_width = fontMetrics().horizontalAdvance(hint.label);
    geometry_.push_back({QStyle::visualRect(layoutDirection(), row,
                                          QRect(x, y, badge_width, badge_height)),
                         QStyle::visualRect(layoutDirection(), row,
                                          QRect(x + badge_width + gap, y,
                                                label_width, badge_height)), index});
    x += badge_width + gap + label_width + metrics_.px(12);
  }
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
  if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange ||
      event->type() == QEvent::LayoutDirectionChange) {
    refresh_geometry();
  } else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::EnabledChange ||
             event->type() == QEvent::ActivationChange) {
    update();
  }
}

void QtOperationInputHints::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setFont(font());
  const int padding = metrics_.px(3);
  const qreal inset = 0.5 / devicePixelRatioF();
  const auto group = !isEnabled() ? QPalette::Disabled :
                     isActiveWindow() ? QPalette::Active : QPalette::Inactive;
  for (const auto &item : geometry_) {
    const auto &hint = hints_[item.hint_index];
    if (!hint.badge.isEmpty()) {
      QPen outline(palette().color(group, hint.active ? QPalette::Highlight : QPalette::Midlight));
      outline.setCosmetic(true);
      painter.setPen(outline);
      painter.setBrush(palette().color(group, hint.active ? QPalette::Highlight : QPalette::Button));
      painter.drawRoundedRect(QRectF(item.badge).adjusted(inset, inset, -inset, -inset),
                              metrics_.px(3), metrics_.px(3));
      painter.setPen(palette().color(group, hint.active ? QPalette::HighlightedText : QPalette::ButtonText));
      painter.drawText(item.badge.adjusted(padding, 0, -padding, 0), Qt::AlignCenter, hint.badge);
    }
    painter.setPen(palette().color(group, hint.active ? QPalette::Text : QPalette::WindowText));
    painter.drawText(item.label, Qt::AlignVCenter |
                     (isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft), hint.label);
  }
}

} // namespace quader
