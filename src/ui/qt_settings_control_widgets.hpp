////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMargins>
#include <QPointer>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStyleOptionFrame>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

#include <functional>
#include <limits>
#include <optional>

namespace quader::qt_settings_detail {

constexpr char kUiScaleMarginsProperty[] =
    "_quader_ui_scale_baseline_margins";
constexpr char kUiScaleSpacingProperty[] =
    "_quader_ui_scale_baseline_spacing";
constexpr char kUiScaleHorizontalSpacingProperty[] =
    "_quader_ui_scale_baseline_horizontal_spacing";
constexpr char kUiScaleVerticalSpacingProperty[] =
    "_quader_ui_scale_baseline_vertical_spacing";
constexpr char kUiScaleMinimumWidthProperty[] =
    "_quader_ui_scale_baseline_minimum_width";

inline void set_scalable_margins(QLayout &layout, const QMargins &baseline) {
  layout.setProperty(kUiScaleMarginsProperty, QVariant::fromValue(baseline));
  layout.setContentsMargins(baseline);
}

inline void set_scalable_spacing(QLayout &layout, int baseline) {
  layout.setProperty(kUiScaleSpacingProperty, baseline);
  layout.setSpacing(baseline);
}

inline void set_scalable_grid_spacing(QGridLayout &layout, int horizontal,
                                     int vertical) {
  layout.setProperty(kUiScaleHorizontalSpacingProperty, horizontal);
  layout.setProperty(kUiScaleVerticalSpacingProperty, vertical);
  layout.setHorizontalSpacing(horizontal);
  layout.setVerticalSpacing(vertical);
}

inline void set_scalable_grid_horizontal_spacing(QGridLayout &layout,
                                                int horizontal) {
  layout.setProperty(kUiScaleHorizontalSpacingProperty, horizontal);
  layout.setHorizontalSpacing(horizontal);
}

inline void set_scalable_minimum_width(QWidget &widget, int baseline) {
  widget.setProperty(kUiScaleMinimumWidthProperty, baseline);
  widget.setMinimumWidth(baseline);
}

inline void apply_scalable_layout_metrics(QObject &root,
                                          const QtUiScaleMetrics &metrics) {
  const QList<QLayout *> layouts = root.findChildren<QLayout *>();
  for (QLayout *layout : layouts) {
    const QVariant margins = layout->property(kUiScaleMarginsProperty);
    if (margins.isValid()) {
      layout->setContentsMargins(
          metrics.margins(margins.value<QMargins>()));
    }
    const QVariant spacing = layout->property(kUiScaleSpacingProperty);
    if (spacing.isValid()) {
      layout->setSpacing(metrics.px(spacing.toInt()));
    }
    auto *grid = dynamic_cast<QGridLayout *>(layout);
    if (grid == nullptr) {
      continue;
    }
    const QVariant horizontal =
        layout->property(kUiScaleHorizontalSpacingProperty);
    if (horizontal.isValid()) {
      grid->setHorizontalSpacing(metrics.px(horizontal.toInt()));
    }
    const QVariant vertical =
        layout->property(kUiScaleVerticalSpacingProperty);
    if (vertical.isValid()) {
      grid->setVerticalSpacing(metrics.px(vertical.toInt()));
    }
  }
}

/// Keeps one disclosure section expanded/collapsed without rebuilding it.
class QtSettingsSection final : public QWidget {
public:
  std::function<void(bool)> expansion_changed;

  explicit QtSettingsSection(QString stable_name,
                                 QWidget *parent = nullptr)
      : QWidget(parent) {
    setObjectName(stable_name);
    auto *layout = new QVBoxLayout(this);
    set_scalable_margins(*layout, QMargins(1, 1, 1, 1));
    set_scalable_spacing(*layout, 0);

    header_ = new QtPanelSectionHeader(this);
    header_->setObjectName(stable_name + QStringLiteral(".header"));
    header_->setChecked(true);
    header_->setArrowType(Qt::DownArrow);
    layout->addWidget(header_);

    content_ = new QFrame(this);
    content_->setObjectName(stable_name + QStringLiteral(".content"));
    content_->setFrameShape(QFrame::NoFrame);
    layout->addWidget(content_);

    QObject::connect(header_, &QToolButton::toggled, this,
                     [this](bool expanded) {
                       const QPointer<QtSettingsSection> guard(this);
                       if (!expanded) {
                         QWidget *focus = QApplication::focusWidget();
                         if (focus != nullptr &&
                             content_->isAncestorOf(focus)) {
                           // Interpret the existing draft before hiding its editor.
                           focus->clearFocus();
                           if (!guard) {
                             return;
                           }
                           header_->setFocus(Qt::OtherFocusReason);
                           if (!guard) {
                             return;
                           }
                         }
                       }
                       if (header_->isChecked() != expanded) {
                         return;
                       }
                       header_->setArrowType(expanded ? Qt::DownArrow
                                                       : Qt::RightArrow);
                       content_->setVisible(expanded);
                       if (!guard) {
                         return;
                       }
                       const auto notify = expansion_changed;
                       if (notify) {
                         notify(expanded);
                       }
                     });
  }

  void set_title(const QString &title) {
    const QString display_title =
        QString(title).replace(QLatin1Char('&'), QStringLiteral("&&"));
    if (header_->text() != display_title) {
      header_->setText(display_title);
    }
    if (header_->accessibleName() != title) {
      header_->setAccessibleName(title);
    }
  }

  [[nodiscard]] QWidget *content() const { return content_; }

  /// Flat property panels share one continuous content surface. Other callers
  /// retain the containing rounded section by default.
  void set_presentation(QtPanelSectionHeader::Presentation presentation) {
    if (header_->presentation() == presentation) {
      return;
    }
    header_->set_presentation(presentation);
    set_scalable_margins(*layout(),
        presentation == QtPanelSectionHeader::Presentation::Flat
            ? QMargins(0, 0, 0, 0)
            : QMargins(1, 1, 1, 1));
    updateGeometry();
    update();
  }

  [[nodiscard]] QtPanelSectionHeader::Presentation presentation() const {
    return header_->presentation();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    header_->apply_ui_scale(metrics);
    update();
  }

  [[nodiscard]] bool expanded() const { return header_->isChecked(); }

  void set_expanded(bool expanded) {
    if (header_->isChecked() != expanded) {
      header_->setChecked(expanded);
    }
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QStyleOptionFrame option;
    option.initFrom(this);
    QStylePainter painter(this);
    if (header_->presentation() == QtPanelSectionHeader::Presentation::Flat) {
      const QPalette::ColorGroup group =
          isEnabled() ? (isActiveWindow() ? QPalette::Active
                                         : QPalette::Inactive)
                      : QPalette::Disabled;
      painter.fillRect(rect(), palette().brush(group, QPalette::Base));
      return;
    }
    if (!qt_application_style_active(&option)) {
      option.lineWidth = style()->pixelMetric(QStyle::PM_DefaultFrameWidth,
                                              &option, this);
      painter.drawPrimitive(QStyle::PE_FrameGroupBox, option);
      return;
    }
    const QPalette::ColorGroup group =
        isEnabled() ? (isActiveWindow() ? QPalette::Active
                                       : QPalette::Inactive)
                    : QPalette::Disabled;
    const qreal line_width =
        1.0 / std::max<qreal>(1.0, devicePixelRatioF());
    const QRectF bounds = QRectF(rect()).adjusted(
        line_width * 0.5, line_width * 0.5,
        -line_width * 0.5, -line_width * 0.5);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(palette().color(group, QPalette::Mid), line_width));
    painter.setBrush(palette().color(group, QPalette::AlternateBase));
    painter.drawRoundedRect(bounds, metrics_.px(6.0), metrics_.px(6.0));
  }

private:
  QtUiScaleMetrics metrics_;
  QtPanelSectionHeader *header_ = nullptr;
  QFrame *content_ = nullptr;
};

inline void apply_scalable_widget_metrics(QWidget &root,
                                          const QtUiScaleMetrics &metrics) {
  const auto apply_metrics = [&metrics](QWidget &widget) {
    if (auto *section = dynamic_cast<QtSettingsSection *>(&widget)) {
      section->apply_ui_scale(metrics);
    } else if (auto *header = dynamic_cast<QtPanelSectionHeader *>(&widget)) {
      header->apply_ui_scale(metrics);
    }
    const QVariant width = widget.property(kUiScaleMinimumWidthProperty);
    if (width.isValid()) {
      widget.setMinimumWidth(metrics.px(width.toInt()));
    }
  };
  apply_metrics(root);
  const QList<QWidget *> widgets = root.findChildren<QWidget *>();
  for (QWidget *widget : widgets) {
    apply_metrics(*widget);
  }
}

/// A mixed value is presentation state, never a sentinel domain value.
class QtMixedNumberSpinBox final : public QtDoubleSpinBox {
public:
  explicit QtMixedNumberSpinBox(double step_origin,
                                  QWidget *parent = nullptr,
                                  bool fixed_scale_format = false)
      : QtDoubleSpinBox(parent), step_origin_(step_origin),
        fixed_scale_format_(fixed_scale_format) {
    setKeyboardTracking(false);
    setDecimals(fixed_scale_format_ ? 13 : 6);
    setRange(-std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max());
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    set_scalable_minimum_width(*this, 88);
    auto &numeric = qt_install_numeric_spin_box(*this);
    numeric.drag_origin = [this]() {
      return mixed_ ? step_origin_ : value();
    };
    numeric.apply_drag_value = [this](double next_value) {
      const bool resolving_mixed = mixed_;
      const double previous = value();
      mixed_ = false;
      const QPointer<QtMixedNumberSpinBox> guard(this);
      setValue(next_value);
      if (guard && resolving_mixed && value() == previous) {
        // Resolving a mixed field is an edit even when its retained scalar
        // already matches; Qt does not emit valueChanged in that case.
        lineEdit()->setText(formatted_value(value()));
        const auto callback = edited;
        if (callback) {
          callback(value());
        }
      }
    };
    QObject::connect(this, &QDoubleSpinBox::valueChanged, this,
                     [this](double value) {
                       const bool was_mixed = mixed_;
                       mixed_ = false;
                       if (was_mixed) {
                         lineEdit()->setText(
                             formatted_value(value));
                       }
                       if (edited) {
                         edited(value);
                       }
                     });
    QObject::connect(lineEdit(), &QLineEdit::textEdited, this,
                     [this](const QString &text) {
                       QString candidate = text;
                       int position = lineEdit()->cursorPosition();
                       if (QDoubleSpinBox::validate(candidate, position) ==
                               QValidator::Acceptable &&
                           text != missing_text_) {
                         mixed_ = false;
                         if (edited) {
                           edited(QtDoubleSpinBox::valueFromText(text));
                         }
                       } else if (edited) {
                         edited(std::nullopt);
                       }
                     });
  }

  void present(std::optional<double> value,
               const QString &missing_text = QStringLiteral("--")) {
    discard_text_edit();
    const QSignalBlocker spin_blocker(this);
    const QSignalBlocker text_blocker(lineEdit());
    missing_text_ = missing_text;
    mixed_ = !value.has_value();
    if (value.has_value() && this->value() != *value) {
      setValue(*value);
    }
    const QString desired = mixed_ ? missing_text_
                                   : formatted_value(this->value());
    if (lineEdit()->text() != desired) {
      lineEdit()->setText(desired);
    }
  }

  /// Clears native text interpretation state when the host abandons a draft.
  void abandon_edit() {
    qt_install_numeric_spin_box(*this).abandon_interaction();
    discard_text_edit();
  }

  std::function<void(std::optional<double>)> edited;

protected:
  QString textFromValue(double value) const override {
    return mixed_ ? missing_text_
                  : formatted_value(value);
  }

  double valueFromText(const QString &text) const override {
    return text == missing_text_
               ? value()
               : QtDoubleSpinBox::valueFromText(text);
  }

  QValidator::State validate(QString &text, int &position) const override {
    return text == missing_text_
               ? QValidator::Intermediate
               : QDoubleSpinBox::validate(text, position);
  }

  void focusInEvent(QFocusEvent *event) override {
    QtDoubleSpinBox::focusInEvent(event);
    if (mixed_) {
      selectAll();
    }
  }

  void stepBy(int steps) override {
    if (isReadOnly()) {
      return;
    }
    if (mixed_) {
      // Arrows on an indeterminate field start from its neutral value.
      mixed_ = false;
      const QSignalBlocker blocker(this);
      setValue(step_origin_);
    }
    QtDoubleSpinBox::stepBy(steps);
  }

private:
  [[nodiscard]] QString formatted_value(double value) const {
    if (!fixed_scale_format_) {
      return QtDoubleSpinBox::textFromValue(value);
    }
    QLocale number_locale = locale();
    number_locale.setNumberOptions(number_locale.numberOptions() |
                                   QLocale::OmitGroupSeparator);
    return number_locale.toString(value, 'f', 4);
  }

  double step_origin_ = 0.0;
  QString missing_text_ = QStringLiteral("--");
  bool mixed_ = false;
  bool fixed_scale_format_ = false;
};

} // namespace quader::qt_settings_detail
