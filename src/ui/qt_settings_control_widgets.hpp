////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <quader/modeling/neutral/polygon/face_texture_scale_steps.hpp>

#include <QDoubleSpinBox>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QLineEdit>
#include <QLocale>
#include <QMargins>
#include <QSignalBlocker>
#include <QSizePolicy>
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

inline void apply_scalable_widget_metrics(QWidget &root,
                                          const QtUiScaleMetrics &metrics) {
  const auto apply_minimum_width = [&metrics](QWidget &widget) {
    const QVariant width = widget.property(kUiScaleMinimumWidthProperty);
    if (width.isValid()) {
      widget.setMinimumWidth(metrics.px(width.toInt()));
    }
  };
  apply_minimum_width(root);
  const QList<QWidget *> widgets = root.findChildren<QWidget *>();
  for (QWidget *widget : widgets) {
    apply_minimum_width(*widget);
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
    set_scalable_margins(*layout, QMargins(0, 0, 0, 0));
    set_scalable_spacing(*layout, 4);

    header_ = new QToolButton(this);
    header_->setObjectName(stable_name + QStringLiteral(".header"));
    header_->setAutoRaise(true);
    header_->setCheckable(true);
    header_->setChecked(true);
    header_->setArrowType(Qt::DownArrow);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    layout->addWidget(header_);

    content_ = new QFrame(this);
    content_->setObjectName(stable_name + QStringLiteral(".content"));
    content_->setFrameShape(QFrame::StyledPanel);
    content_->setFrameShadow(QFrame::Plain);
    layout->addWidget(content_);

    QObject::connect(header_, &QToolButton::toggled, this,
                     [this](bool expanded) {
                       header_->setArrowType(expanded ? Qt::DownArrow
                                                       : Qt::RightArrow);
                       content_->setVisible(expanded);
                       if (expansion_changed) {
                         expansion_changed(expanded);
                       }
                     });
  }

  void set_title(const QString &title) {
    if (header_->text() != title) {
      header_->setText(title);
    }
    if (header_->accessibleName() != title) {
      header_->setAccessibleName(title);
    }
  }

  [[nodiscard]] QWidget *content() const { return content_; }

  [[nodiscard]] bool expanded() const { return header_->isChecked(); }

  void set_expanded(bool expanded) {
    if (header_->isChecked() != expanded) {
      header_->setChecked(expanded);
    }
  }

private:
  QToolButton *header_ = nullptr;
  QFrame *content_ = nullptr;
};

/// A mixed value is presentation state, never a sentinel domain value.
class QtMixedNumberSpinBox final : public QtDoubleSpinBox {
public:
  explicit QtMixedNumberSpinBox(double step_origin,
                                  QWidget *parent = nullptr,
                                  bool scale_steps = false)
      : QtDoubleSpinBox(parent), step_origin_(step_origin),
        scale_steps_(scale_steps) {
    setKeyboardTracking(false);
    setDecimals(scale_steps_ ? 13 : 6);
    setRange(-std::numeric_limits<float>::max(),
             std::numeric_limits<float>::max());
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    set_scalable_minimum_width(*this, 88);
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
  void abandon_edit() { discard_text_edit(); }

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
    if (scale_steps_) {
      interpretText();
      setValue(quader_poly::stepped_face_texture_scale(value(), steps));
    } else {
      QtDoubleSpinBox::stepBy(steps);
    }
  }

  StepEnabled stepEnabled() const override {
    if (!scale_steps_) {
      return QtDoubleSpinBox::stepEnabled();
    }
    if (isReadOnly()) {
      return StepNone;
    }
    const double current = mixed_ ? step_origin_ : value();
    StepEnabled enabled = StepNone;
    if (quader_poly::stepped_face_texture_scale(current, 1) != current) {
      enabled |= StepUpEnabled;
    }
    if (quader_poly::stepped_face_texture_scale(current, -1) != current) {
      enabled |= StepDownEnabled;
    }
    return enabled;
  }

private:
  [[nodiscard]] QString formatted_value(double value) const {
    if (!scale_steps_) {
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
  bool scale_steps_ = false;
};

} // namespace quader::qt_settings_detail
