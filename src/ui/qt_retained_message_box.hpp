////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_ui_scale_metrics.hpp"

#include <QIcon>
#include <QMessageBox>
#include <QString>

#include <array>
#include <cstdint>
#include <vector>

class QCloseEvent;
class QEvent;
class QLabel;
class QPushButton;
class QWidget;

namespace quader {

/// Stable identities for the three retained button widgets.
enum class QtRetainedMessageBoxButton : std::uint8_t {
  Primary,
  Secondary,
  Tertiary,
  None,
};

/// Per-presentation configuration for one retained button widget.
struct QtRetainedMessageBoxButtonPresentation final {
  QtRetainedMessageBoxButton button = QtRetainedMessageBoxButton::None;
  QString object_name;
  QString label;
  QMessageBox::ButtonRole role = QMessageBox::InvalidRole;
};

/// Complete content and button policy for one modal presentation.
struct QtRetainedMessageBoxPresentation final {
  QMessageBox::Icon icon = QMessageBox::NoIcon;
  QString object_name;
  QString title;
  QString text;
  QString informative_text;
  std::vector<QtRetainedMessageBoxButtonPresentation> buttons;
  QtRetainedMessageBoxButton default_button =
      QtRetainedMessageBoxButton::None;
  QtRetainedMessageBoxButton escape_button =
      QtRetainedMessageBoxButton::None;
  QtRetainedMessageBoxButton close_button =
      QtRetainedMessageBoxButton::None;
};

/**
 * Reuses one QMessageBox, its native window, child layout, icon resources, and
 * three button widgets across differently configured modal presentations.
 */
class QtRetainedMessageBox : public QMessageBox {
public:
  explicit QtRetainedMessageBox(QWidget *parent = nullptr);

  /// Applies content without presenting the dialog. Invalid policies are
  /// rejected without partially changing the retained presentation.
  [[nodiscard]] bool
  set_presentation(const QtRetainedMessageBoxPresentation &presentation);

  /// Presents the configured dialog and returns the stable retained button.
  [[nodiscard]] QtRetainedMessageBoxButton
  present(const QtRetainedMessageBoxPresentation &presentation);

  /// Resolves all standard icons once and ensures a hidden native window.
  void prewarm();

  /// Refreshes retained style-owned resources after an interface-scale event.
  void apply_ui_scale(const QtUiScaleMetrics &metrics);

  /// Hides instead of closing so the prewarmed native window remains alive.
  void done(int result) override;

  [[nodiscard]] QPushButton *
  retained_button(QtRetainedMessageBoxButton button) const noexcept;
  [[nodiscard]] QMessageBox::Icon presented_icon() const noexcept {
    return presented_icon_;
  }

protected:
  bool event(QEvent *event) override;
  void closeEvent(QCloseEvent *event) override;

private:
  static constexpr std::size_t kRetainedButtonCount = 3u;

  [[nodiscard]] static std::size_t
  button_index(QtRetainedMessageBoxButton button) noexcept;
  [[nodiscard]] static std::size_t
  icon_index(QMessageBox::Icon icon) noexcept;
  [[nodiscard]] bool
  presentation_is_valid(const QtRetainedMessageBoxPresentation &presentation)
      const noexcept;
  void apply_informative_text(const QString &text);
  void refresh_standard_icon_cache();
  void apply_presented_icon();

  std::array<QPushButton *, kRetainedButtonCount> buttons_{};
  std::array<QMessageBox::ButtonRole, kRetainedButtonCount> button_roles_{};
  std::array<QIcon, 4u> standard_icon_cache_{};
  QMessageBox::Icon presented_icon_ = QMessageBox::NoIcon;
  QtRetainedMessageBoxButton selected_button_ =
      QtRetainedMessageBoxButton::None;
  QtRetainedMessageBoxButton close_button_ =
      QtRetainedMessageBoxButton::None;
  int message_icon_size_ = 0;
  qreal message_icon_device_pixel_ratio_ = 1.0;
  bool prewarmed_ = false;
  QtUiScaleMetrics ui_scale_metrics_;
};

} // namespace quader
