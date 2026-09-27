////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_control_strip.hpp"

#include "ui/qt_application_style.hpp"
#include "ui/qt_multiresolution_icon.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"

#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QStyleOption>
#include <QToolButton>

#include <algorithm>

namespace quader {
namespace {

class QtViewportStripSeparator final : public QWidget {
public:
  explicit QtViewportStripSeparator(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    const qreal hairline = 1.0 / devicePixelRatioF();
    painter.fillRect(QRectF(width() / 2.0, 0.0, hairline, height()),
                     palette().brush(QPalette::Mid));
  }
};

int control_width(const QWidget *widget, bool minimum = false) {
  if (widget == nullptr) {
    return 0;
  }
  const QSize hint = minimum ? widget->minimumSizeHint() : widget->sizeHint();
  return std::clamp(hint.width(), widget->minimumWidth(),
                    widget->maximumWidth());
}

} // namespace

const QtViewportControlStrip *
qt_viewport_control_strip_ancestor(const QObject *object) {
  for (const QObject *current = object; current != nullptr;
       current = current->parent()) {
    if (const auto *strip = dynamic_cast<const QtViewportControlStrip *>(current)) {
      return strip;
    }
  }
  return nullptr;
}

QtViewportControlStrip::QtViewportControlStrip(QWidget *parent)
    : QWidget(parent), layout_(new QHBoxLayout(this)) {
  setObjectName(QStringLiteral("viewportControlStrip"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(metrics_.px(36));
  layout_->setSizeConstraint(QLayout::SetNoConstraint);
  layout_->setContentsMargins(metrics_.margins(QMargins(4, 4, 4, 4)));
  layout_->setSpacing(metrics_.px(2));
  layout_->addStretch();
  button_style_ = &install_native_toolbar_button_presentation(*this);
}

QtViewportControlStrip::~QtViewportControlStrip() {
  updating_overflow_ = true;
  // Actions are added after the shared style. Destroy their widgets explicitly
  // while that host-owned style is still alive.
  for (const auto &button : buttons_) {
    delete button.data();
  }
  delete overflow_button_;
  overflow_button_ = nullptr;
  delete environment_.data();
  delete camera_.data();
  delete shading_.data();
  delete layout_control_.data();
  delete camera_settings_.data();
}

void QtViewportControlStrip::add_action(QAction &action) {
  auto *button = new QToolButton(this);
  button->setObjectName(QStringLiteral("viewportStrip_%1").arg(action.objectName()));
  button->setDefaultAction(&action);
  if (action.menu() != nullptr) {
    button->setPopupMode(QToolButton::DelayedPopup);
  }
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
  configure_native_toolbar_button_face(*button, action, *button_style_);
  scale_button(*button);
  layout_->insertWidget(static_cast<int>(entries_.size()), button, 0,
                         Qt::AlignVCenter);
  buttons_.push_back(button);
  entries_.push_back({&action, button, false});
  QObject::connect(&action, &QAction::changed, this,
                   [this]() { update_overflow(); });
  QObject::connect(&action, &QObject::destroyed, this,
                   [this]() { update_overflow(); });
  update_overflow();
  updateGeometry();
}

void QtViewportControlStrip::add_separator() {
  auto *separator = new QtViewportStripSeparator(this);
  separator->setFixedSize(metrics_.size(QSize(8, 20)));
  layout_->insertWidget(static_cast<int>(entries_.size()), separator, 0,
                         Qt::AlignVCenter);
  separators_.push_back(separator);
  entries_.push_back({nullptr, separator, true});
  update_overflow();
  updateGeometry();
}

void QtViewportControlStrip::set_trailing_controls(
    QWidget *camera, QWidget *shading, QWidget *environment,
    QAction *environment_overflow, QWidget *layout, QWidget *camera_settings) {
  for (QWidget *control : {environment_.data(), camera_.data(),
                           shading_.data(), layout_control_.data(), camera_settings_.data()}) {
    if (control != nullptr) {
      layout_->removeWidget(control);
      control->hide();
    }
  }
  if (environment_overflow_ != nullptr) {
    QObject::disconnect(environment_overflow_, nullptr, this, nullptr);
  }
  camera_ = camera;
  shading_ = shading;
  layout_control_ = layout;
  camera_settings_ = camera_settings;
  environment_ = environment;
  environment_overflow_ = environment_overflow;
  for (QWidget *control : {environment, camera, shading, layout, camera_settings}) {
    if (control != nullptr) {
      layout_->addWidget(control, 0, Qt::AlignVCenter);
      control->show();
    }
  }
  if (environment_overflow != nullptr) {
    QObject::connect(environment_overflow, &QAction::changed, this,
                     [this]() { update_overflow(); });
  }
  update_overflow();
  updateGeometry();
}

void QtViewportControlStrip::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (metrics_.factor() == metrics.factor() &&
      metrics_.revision() == metrics.revision()) {
    return;
  }
  metrics_ = metrics;
  setFixedHeight(metrics_.px(36));
  layout_->setContentsMargins(metrics_.margins(QMargins(4, 4, 4, 4)));
  layout_->setSpacing(metrics_.px(2));
  for (const auto &button : buttons_) {
    if (button != nullptr) {
      scale_button(*button);
    }
  }
  for (auto *separator : separators_) {
    separator->setFixedSize(metrics_.size(QSize(8, 20)));
  }
  if (overflow_button_ != nullptr) {
    scale_button(*overflow_button_);
    update_overflow_icon();
  }
  // Release the previous scale's layout minimum before a parent resizes us.
  layout_->invalidate();
  update_overflow();
  layout_->activate();
  updateGeometry();
}

QSize QtViewportControlStrip::sizeHint() const {
  int width = metrics_.px(8);
  int count = 0;
  for (const Entry &entry : entries_) {
    if (entry.widget != nullptr &&
        (entry.separator ||
         (entry.action != nullptr && entry.action->isVisible()))) {
      width += metrics_.px(entry.separator ? 8 : 28);
      ++count;
    }
  }
  for (const QWidget *control : {environment_.data(), camera_.data(),
                                 shading_.data(), layout_control_.data(), camera_settings_.data()}) {
    if (control != nullptr) {
      width += control_width(control);
      ++count;
    }
  }
  return {width + std::max(0, count - 1) * metrics_.px(2), metrics_.px(36)};
}

QSize QtViewportControlStrip::minimumSizeHint() const {
  int width = metrics_.px(8);
  int count = 0;
  for (const QWidget *control : {camera_.data(), shading_.data(),
                                layout_control_.data(), camera_settings_.data()}) {
    if (control != nullptr) {
      width += control_width(control, true);
      ++count;
    }
  }
  if (environment_ != nullptr && environment_overflow_ == nullptr) {
    width += control_width(environment_, true);
    ++count;
  }
  if (!entries_.empty() || environment_overflow_ != nullptr) {
    width += metrics_.px(28);
    ++count;
  }
  return {width + std::max(0, count - 1) * metrics_.px(2), metrics_.px(36)};
}

void QtViewportControlStrip::ensure_overflow_button() {
  if (overflow_button_ != nullptr) {
    return;
  }
  overflow_action_ = new QAction(tr("More viewport controls"), this);
  overflow_action_->setObjectName(QStringLiteral("viewportStripOverflowAction"));
  overflow_button_ = new QToolButton(this);
  overflow_button_->setObjectName(QStringLiteral("viewportStripOverflow"));
  overflow_button_->setDefaultAction(overflow_action_);
  overflow_button_->setToolButtonStyle(Qt::ToolButtonIconOnly);
  configure_native_toolbar_button_face(*overflow_button_, *overflow_action_,
                                        *button_style_);
  scale_button(*overflow_button_);
  overflow_menu_ = new QMenu(overflow_button_);
  overflow_menu_->setObjectName(QStringLiteral("viewportStripOverflowMenu"));
  overflow_button_->setMenu(overflow_menu_);
  overflow_button_->setPopupMode(QToolButton::InstantPopup);
  layout_->insertWidget(static_cast<int>(entries_.size()), overflow_button_, 0,
                         Qt::AlignVCenter);
  update_overflow_icon();
}

void QtViewportControlStrip::update_overflow_icon() {
  if (overflow_action_ != nullptr && overflow_button_ != nullptr) {
    QStyleOption option;
    option.initFrom(this);
    const QIcon native = style()->standardIcon(
        QStyle::SP_ToolBarHorizontalExtensionButton, &option, overflow_button_);
    // Fusion's extension artwork contains dark RGB values rather than a
    // palette-aware foreground. Keep its native shape and RTL direction while
    // using the same tinting owner as Quader's other monochrome chrome.
    const QImage source = native.pixmap(QSize(96, 96)).toImage();
    const auto enabled_group = option.state.testFlag(QStyle::State_Active)
        ? QPalette::Active : QPalette::Inactive;
    const QSize size = overflow_button_->iconSize();
    const qreal ratio = overflow_button_->devicePixelRatioF();
    const QPixmap enabled = qt_multiresolution_icon(
        source, option.palette.color(enabled_group, QPalette::ButtonText))
        .pixmap(size, ratio);
    const QPixmap disabled = qt_multiresolution_icon(
        source, option.palette.color(QPalette::Disabled, QPalette::ButtonText))
        .pixmap(size, ratio);
    QIcon icon;
    for (const auto mode : {QIcon::Normal, QIcon::Active, QIcon::Selected}) {
      icon.addPixmap(enabled, mode);
    }
    icon.addPixmap(disabled, QIcon::Disabled);
    overflow_action_->setIcon(icon);
  }
}

void QtViewportControlStrip::update_overflow() {
  if (updating_overflow_ || layout_ == nullptr) {
    return;
  }
  updating_overflow_ = true;
  const int spacing = metrics_.px(2);
  std::vector<std::size_t> eligible;
  std::size_t separator = entries_.size();
  for (std::size_t index = 0; index < entries_.size(); ++index) {
    const Entry &entry = entries_[index];
    if (entry.separator) {
      if (!eligible.empty()) {
        separator = index;
      }
    } else if (entry.widget != nullptr && entry.action != nullptr &&
               entry.action->isVisible()) {
      if (separator != entries_.size()) {
        eligible.push_back(separator);
        separator = entries_.size();
      }
      eligible.push_back(index);
    }
  }
  const auto entry_width = [&](std::size_t index) {
    return metrics_.px(entries_[index].separator ? 8 : 28);
  };
  int action_width = 0;
  for (const std::size_t index : eligible) {
    action_width += entry_width(index) + (action_width != 0 ? spacing : 0);
  }
  int trailing_width = 0;
  for (QWidget *control : {camera_.data(), shading_.data(),
                          layout_control_.data(), camera_settings_.data()}) {
    if (control != nullptr) {
      trailing_width += control_width(control) +
                        (trailing_width != 0 ? spacing : 0);
    }
  }
  bool show_environment = environment_ != nullptr &&
      (environment_overflow_ == nullptr || environment_overflow_->isVisible());
  const int environment_width = show_environment
      ? control_width(environment_) + (trailing_width != 0 ? spacing : 0) : 0;
  const int available = std::max(0, width() - metrics_.px(8));
  const int total_trailing = trailing_width + environment_width;
  const bool can_overflow = !eligible.empty() ||
      (show_environment && environment_overflow_ != nullptr);
  const bool overflow = can_overflow && action_width + total_trailing +
      (action_width != 0 && total_trailing != 0 ? spacing : 0) > available;
  int action_budget = available - total_trailing -
      (total_trailing != 0 && action_width != 0 ? spacing : 0);
  if (overflow) {
    ensure_overflow_button();
    if (show_environment && environment_overflow_ != nullptr &&
        total_trailing + metrics_.px(28) + spacing > available) {
      show_environment = false;
    }
    const int reserved = trailing_width +
        (show_environment ? environment_width : 0);
    action_budget = available - reserved - metrics_.px(28) -
        (reserved != 0 ? spacing : 0) - spacing;
  }
  std::size_t visible_count = 0;
  int used = 0;
  for (const std::size_t index : eligible) {
    const int next = used + (visible_count != 0 ? spacing : 0) + entry_width(index);
    if (next > action_budget) {
      break;
    }
    used = next;
    ++visible_count;
  }
  while (visible_count != 0 && entries_[eligible[visible_count - 1]].separator) {
    --visible_count;
  }
  std::vector<bool> visible(entries_.size(), false);
  for (std::size_t index = 0; index < visible_count; ++index) {
    visible[eligible[index]] = true;
  }
  for (std::size_t index = 0; index < entries_.size(); ++index) {
    if (QWidget *widget = entries_[index].widget; widget != nullptr &&
        widget->isHidden() == visible[index]) {
      widget->setVisible(visible[index]);
    }
  }
  if (environment_ != nullptr && environment_->isHidden() == show_environment) {
    environment_->setVisible(show_environment);
  }
  if (overflow_button_ != nullptr) {
    if (overflow_button_->isHidden() == overflow) {
      overflow_button_->setVisible(overflow);
    }
    QList<QAction *> desired;
    bool pending_separator = false;
    for (std::size_t index = visible_count; index < eligible.size(); ++index) {
      const Entry &entry = entries_[eligible[index]];
      if (entry.separator) {
        pending_separator = !desired.isEmpty();
      } else {
        if (pending_separator) {
          desired.push_back(nullptr);
          pending_separator = false;
        }
        desired.push_back(entry.action);
      }
    }
    if (!show_environment && environment_overflow_ != nullptr &&
        environment_overflow_->isVisible()) {
      if (!desired.isEmpty()) {
        desired.push_back(nullptr);
      }
      desired.push_back(environment_overflow_);
    }
    const QList<QAction *> current = overflow_menu_->actions();
    bool menu_changed = current.size() != desired.size();
    for (qsizetype index = 0; !menu_changed && index < desired.size(); ++index) {
      menu_changed = desired[index] == nullptr
          ? !current[index]->isSeparator() : current[index] != desired[index];
    }
    if (menu_changed) {
      overflow_menu_->clear();
      for (QAction *action : desired) {
        if (action != nullptr) {
          overflow_menu_->addAction(action);
        } else {
          overflow_menu_->addSeparator();
        }
      }
    }
  }
  QWidget *previous = nullptr;
  const auto append_tab_stop = [&previous](QWidget *widget) {
    if (widget != nullptr && !widget->isHidden() &&
        widget->focusPolicy() != Qt::NoFocus) {
      if (previous != nullptr) {
        QWidget::setTabOrder(previous, widget);
      }
      previous = widget;
    }
  };
  for (std::size_t index = 0; index < visible_count; ++index) {
    append_tab_stop(entries_[eligible[index]].widget);
  }
  append_tab_stop(overflow_button_);
  append_tab_stop(environment_);
  append_tab_stop(camera_);
  append_tab_stop(shading_);
  append_tab_stop(layout_control_);
  append_tab_stop(camera_settings_);
  layout_->activate();
  updating_overflow_ = false;
}

bool QtViewportControlStrip::event(QEvent *event) {
  const bool handled = QWidget::event(event);
  if (event->type() == QEvent::LayoutRequest ||
      event->type() == QEvent::FontChange ||
      event->type() == QEvent::StyleChange ||
      event->type() == QEvent::LayoutDirectionChange) {
    update_overflow();
  }
  if (event->type() == QEvent::StyleChange ||
      event->type() == QEvent::LayoutDirectionChange ||
      event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::ActivationChange ||
      event->type() == QEvent::WindowActivate ||
      event->type() == QEvent::WindowDeactivate ||
      event->type() == QEvent::Show ||
      event->type() == QEvent::DevicePixelRatioChange) {
    update_overflow_icon();
  }
  return handled;
}

void QtViewportControlStrip::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  update_overflow();
}

void QtViewportControlStrip::scale_button(QToolButton &button) {
  apply_native_toolbar_button_face_ui_scale(button, metrics_);
  button.setFixedSize(metrics_.size(QSize(28, 28)));
  button.setIconSize(metrics_.size(QSize(24, 24)));
}

void QtViewportControlStrip::paintEvent(QPaintEvent *) {
  QStyleOption option;
  option.initFrom(this);
  QPainter painter(this);
  painter.fillRect(rect(), option.palette.brush(
                               qt_application_style_active(&option)
                                   ? QPalette::AlternateBase
                                   : QPalette::Window));
  const qreal hairline = 1.0 / devicePixelRatioF();
  painter.fillRect(QRectF(0.0, height() - hairline, width(), hairline),
                   option.palette.brush(QPalette::Mid));
}

} // namespace quader
