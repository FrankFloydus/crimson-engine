////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_retained_message_box.hpp"

#include <QCloseEvent>
#include <QEvent>
#include <QLabel>
#include <QLayout>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QStyle>

#include <algorithm>
#include <array>
#include <limits>

namespace quader {

namespace {

constexpr std::size_t kInvalidButtonIndex =
    std::numeric_limits<std::size_t>::max();

constexpr std::array<QtRetainedMessageBoxButton, 3u> kRetainedButtons{
    QtRetainedMessageBoxButton::Primary,
    QtRetainedMessageBoxButton::Secondary,
    QtRetainedMessageBoxButton::Tertiary,
};

constexpr std::array<QStyle::StandardPixmap, 4u> kStandardMessageIcons{
    QStyle::SP_MessageBoxCritical,
    QStyle::SP_MessageBoxWarning,
    QStyle::SP_MessageBoxQuestion,
    QStyle::SP_MessageBoxInformation,
};

} // namespace

QtRetainedMessageBox::QtRetainedMessageBox(QWidget *parent)
    : QMessageBox(QMessageBox::NoIcon, QString{}, QString{},
                  QMessageBox::NoButton, parent) {
  setWindowModality(Qt::WindowModal);
  setAttribute(Qt::WA_DeleteOnClose, false);
  setOption(QMessageBox::Option::DontUseNativeDialog);

  // QMessageBox replaces its top-level layout when an informative label or
  // icon is first introduced. Create both structural children once, then
  // update their retained contents directly for every presentation.
  setInformativeText(QStringLiteral(" "));
  QPixmap icon_placeholder(1, 1);
  icon_placeholder.fill(Qt::transparent);
  setIconPixmap(icon_placeholder);
  apply_informative_text(QString{});
  if (QLabel *const icon_label = findChild<QLabel *>(
          QStringLiteral("qt_msgboxex_icon_label"))) {
    icon_label->clear();
    icon_label->setVisible(false);
  }

  constexpr std::array<QMessageBox::ButtonRole, kRetainedButtonCount>
      kInitialRoles{
          QMessageBox::AcceptRole,
          QMessageBox::DestructiveRole,
          QMessageBox::RejectRole,
      };
  constexpr std::array<const char *, kRetainedButtonCount>
      kInitialObjectNames{
          "retainedMessageBox.primary",
          "retainedMessageBox.secondary",
          "retainedMessageBox.tertiary",
      };

  for (std::size_t index = 0u; index < buttons_.size(); ++index) {
    QPushButton *const button =
        addButton(QStringLiteral(" "), kInitialRoles[index]);
    button->setObjectName(QString::fromLatin1(kInitialObjectNames[index]));
    button->setVisible(false);
    buttons_[index] = button;
    button_roles_[index] = kInitialRoles[index];
    const QtRetainedMessageBoxButton button_id = kRetainedButtons[index];
    connect(button, &QPushButton::clicked, this,
            [this, button_id]() { selected_button_ = button_id; });
  }
}

bool QtRetainedMessageBox::set_presentation(
    const QtRetainedMessageBoxPresentation &presentation) {
  if (!presentation_is_valid(presentation) || isVisible()) {
    return false;
  }

  std::array<const QtRetainedMessageBoxButtonPresentation *,
             kRetainedButtonCount>
      button_presentations{};
  for (const QtRetainedMessageBoxButtonPresentation &button :
       presentation.buttons) {
    button_presentations[button_index(button.button)] = &button;
  }

  setObjectName(presentation.object_name);
  setWindowTitle(presentation.title);
  setText(presentation.text);
  apply_informative_text(presentation.informative_text);
  presented_icon_ = presentation.icon;
  apply_presented_icon();

  for (std::size_t index = 0u; index < buttons_.size(); ++index) {
    QPushButton *const retained = buttons_[index];
    const QtRetainedMessageBoxButtonPresentation *const button =
        button_presentations[index];
    if (button == nullptr) {
      retained->setVisible(false);
      continue;
    }

    if (button_roles_[index] != button->role) {
      removeButton(retained);
      addButton(retained, button->role);
      button_roles_[index] = button->role;
    }
    retained->setObjectName(button->object_name);
    retained->setText(button->label);
    retained->setVisible(true);
  }

  setDefaultButton(retained_button(presentation.default_button));
  setEscapeButton(retained_button(presentation.escape_button));
  close_button_ = presentation.close_button;
  selected_button_ = close_button_;
  setResult(QDialog::Rejected);
  return true;
}

QtRetainedMessageBoxButton QtRetainedMessageBox::present(
    const QtRetainedMessageBoxPresentation &presentation) {
  if (!set_presentation(presentation)) {
    return QtRetainedMessageBoxButton::None;
  }

  prewarm();
  if (QLayout *dialog_layout = layout()) {
    (void)dialog_layout->activate();
  }
  adjustSize();
  exec();
  return selected_button_;
}

void QtRetainedMessageBox::prewarm() {
  if (prewarmed_ || isVisible()) {
    return;
  }

  ensurePolished();
  for (QPushButton *const button : buttons_) {
    button->ensurePolished();
  }

  refresh_standard_icon_cache();
  prewarmed_ = true;
  apply_presented_icon();
  if (QLayout *dialog_layout = layout()) {
    (void)dialog_layout->activate();
  }
  adjustSize();

  // winId() creates the hidden platform window without mapping it or entering
  // a nested event loop. done() hides rather than closing this native owner.
  (void)winId();
}

void QtRetainedMessageBox::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  if (!prewarmed_) {
    return;
  }
  refresh_standard_icon_cache();
  apply_presented_icon();
  if (QLayout *dialog_layout = layout()) {
    dialog_layout->invalidate();
    (void)dialog_layout->activate();
  }
  if (isVisible()) {
    adjustSize();
  }
  update();
}

void QtRetainedMessageBox::done(int result) {
  const QPointer<QtRetainedMessageBox> guard(this);
  setResult(result);
  hide();
  if (guard == nullptr) {
    return;
  }

  int dialog_code = result;
  if (dialog_code > QDialog::Accepted) {
    switch (buttonRole(clickedButton())) {
    case QMessageBox::AcceptRole:
    case QMessageBox::YesRole:
      dialog_code = QDialog::Accepted;
      break;
    case QMessageBox::RejectRole:
    case QMessageBox::NoRole:
      dialog_code = QDialog::Rejected;
      break;
    default:
      break;
    }
  }
  if (dialog_code == QDialog::Accepted) {
    emit accepted();
  } else if (dialog_code == QDialog::Rejected) {
    emit rejected();
  }
  if (guard != nullptr) {
    emit finished(result);
  }
}

QPushButton *QtRetainedMessageBox::retained_button(
    QtRetainedMessageBoxButton button) const noexcept {
  const std::size_t index = button_index(button);
  return index < buttons_.size() ? buttons_[index] : nullptr;
}

void QtRetainedMessageBox::closeEvent(QCloseEvent *event) {
  selected_button_ = close_button_;
  done(QDialog::Rejected);
  event->ignore();
}

bool QtRetainedMessageBox::event(QEvent *event) {
  const bool handled = QMessageBox::event(event);
  if (prewarmed_ &&
      (event->type() == QEvent::DevicePixelRatioChange ||
       event->type() == QEvent::PaletteChange ||
       event->type() == QEvent::ApplicationPaletteChange)) {
    refresh_standard_icon_cache();
    apply_presented_icon();
  }
  return handled;
}

std::size_t QtRetainedMessageBox::button_index(
    QtRetainedMessageBoxButton button) noexcept {
  switch (button) {
  case QtRetainedMessageBoxButton::Primary:
    return 0u;
  case QtRetainedMessageBoxButton::Secondary:
    return 1u;
  case QtRetainedMessageBoxButton::Tertiary:
    return 2u;
  case QtRetainedMessageBoxButton::None:
    break;
  }
  return kInvalidButtonIndex;
}

std::size_t
QtRetainedMessageBox::icon_index(QMessageBox::Icon icon) noexcept {
  switch (icon) {
  case QMessageBox::Critical:
    return 0u;
  case QMessageBox::Warning:
    return 1u;
  case QMessageBox::Question:
    return 2u;
  case QMessageBox::Information:
    return 3u;
  case QMessageBox::NoIcon:
    break;
  }
  return kInvalidButtonIndex;
}

bool QtRetainedMessageBox::presentation_is_valid(
    const QtRetainedMessageBoxPresentation &presentation) const noexcept {
  if (presentation.buttons.empty() ||
      presentation.buttons.size() > buttons_.size()) {
    return false;
  }

  std::array<bool, kRetainedButtonCount> active{};
  for (const QtRetainedMessageBoxButtonPresentation &button :
       presentation.buttons) {
    const std::size_t index = button_index(button.button);
    if (index >= active.size() || active[index] ||
        button.role == QMessageBox::InvalidRole) {
      return false;
    }
    active[index] = true;
  }

  const auto is_active = [&active](QtRetainedMessageBoxButton button) {
    const std::size_t index = button_index(button);
    return index < active.size() && active[index];
  };
  return (presentation.default_button == QtRetainedMessageBoxButton::None ||
          is_active(presentation.default_button)) &&
         (presentation.escape_button == QtRetainedMessageBoxButton::None ||
          is_active(presentation.escape_button)) &&
         is_active(presentation.close_button);
}

void QtRetainedMessageBox::apply_informative_text(const QString &text) {
  if (QLabel *const informative_label = findChild<QLabel *>(
          QStringLiteral("qt_msgbox_informativelabel"))) {
    informative_label->setText(text);
    informative_label->setVisible(!text.isEmpty());
  }
}

void QtRetainedMessageBox::refresh_standard_icon_cache() {
  QStyle *const dialog_style = style();
  if (dialog_style == nullptr) {
    message_icon_size_ = 0;
    standard_icon_cache_.fill(QIcon{});
    return;
  }

  message_icon_size_ = dialog_style->pixelMetric(
      QStyle::PM_MessageBoxIconSize, nullptr, this);
  message_icon_device_pixel_ratio_ =
      std::max<qreal>(1.0, devicePixelRatioF());
  for (std::size_t index = 0u; index < kStandardMessageIcons.size();
       ++index) {
    standard_icon_cache_[index] = dialog_style->standardIcon(
        kStandardMessageIcons[index], nullptr, this);
    if (message_icon_size_ > 0) {
      (void)standard_icon_cache_[index].pixmap(
          QSize(message_icon_size_, message_icon_size_),
          message_icon_device_pixel_ratio_);
    }
  }
}

void QtRetainedMessageBox::apply_presented_icon() {
  QLabel *const icon_label = findChild<QLabel *>(
      QStringLiteral("qt_msgboxex_icon_label"));
  if (icon_label == nullptr) {
    return;
  }

  const std::size_t index = icon_index(presented_icon_);
  if (!prewarmed_ || index >= standard_icon_cache_.size() ||
      message_icon_size_ <= 0) {
    icon_label->clear();
    icon_label->setVisible(false);
    return;
  }
  icon_label->setPixmap(standard_icon_cache_[index].pixmap(
      QSize(message_icon_size_, message_icon_size_),
      message_icon_device_pixel_ratio_));
  icon_label->setVisible(true);
}

} // namespace quader
