////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_number_slider.hpp"

#include <QMouseEvent>
#include <QStyle>
#include <QStyleOptionSlider>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

std::pair<double, double> sanitized_bounds(double minimum, double maximum) {
  if (!std::isfinite(minimum)) {
    minimum = 0.0;
  }
  if (!std::isfinite(maximum)) {
    maximum = minimum;
  }
  if (maximum < minimum) {
    std::swap(minimum, maximum);
  }
  return {minimum, maximum};
}

int integer_bound(double value) {
  constexpr double kMinimum =
      static_cast<double>(std::numeric_limits<int>::min());
  constexpr double kMaximum =
      static_cast<double>(std::numeric_limits<int>::max());
  return static_cast<int>(std::llround(std::clamp(value, kMinimum, kMaximum)));
}

double signed_logarithm(double value, double scale) {
  return std::copysign(std::log1p(std::abs(value) / scale), value);
}

double inverse_signed_logarithm(double value, double scale) {
  return std::copysign(scale * std::expm1(std::abs(value)), value);
}

double normalized_slider_fraction(
    const QtToolSettingsNumberSliderMapping &mapping, double value) {
  const double span = mapping.value_maximum - mapping.value_minimum;
  const double scale = mapping.curve_scale;
  switch (mapping.curve) {
  case QtToolSettingsNumberSliderCurve::Linear:
    return (value - mapping.value_minimum) / span;
  case QtToolSettingsNumberSliderCurve::LogarithmicFromMinimum:
    return std::log1p((value - mapping.value_minimum) / scale) /
           std::log1p(span / scale);
  case QtToolSettingsNumberSliderCurve::LogarithmicFromMaximum:
    return 1.0 -
           std::log1p((mapping.value_maximum - value) / scale) /
               std::log1p(span / scale);
  case QtToolSettingsNumberSliderCurve::SymmetricLogarithmic: {
    const double transformed_minimum =
        signed_logarithm(mapping.value_minimum, scale);
    const double transformed_maximum =
        signed_logarithm(mapping.value_maximum, scale);
    return (signed_logarithm(value, scale) - transformed_minimum) /
           (transformed_maximum - transformed_minimum);
  }
  }
  return 0.0;
}

double value_from_normalized_slider_fraction(
    const QtToolSettingsNumberSliderMapping &mapping, double normalized) {
  const double span = mapping.value_maximum - mapping.value_minimum;
  const double scale = mapping.curve_scale;
  switch (mapping.curve) {
  case QtToolSettingsNumberSliderCurve::Linear:
    return mapping.value_minimum + span * normalized;
  case QtToolSettingsNumberSliderCurve::LogarithmicFromMinimum:
    return mapping.value_minimum +
           scale * std::expm1(normalized * std::log1p(span / scale));
  case QtToolSettingsNumberSliderCurve::LogarithmicFromMaximum:
    return mapping.value_maximum -
           scale *
               std::expm1((1.0 - normalized) * std::log1p(span / scale));
  case QtToolSettingsNumberSliderCurve::SymmetricLogarithmic: {
    const double transformed_minimum =
        signed_logarithm(mapping.value_minimum, scale);
    const double transformed_maximum =
        signed_logarithm(mapping.value_maximum, scale);
    return inverse_signed_logarithm(
        transformed_minimum +
            (transformed_maximum - transformed_minimum) * normalized,
        scale);
  }
  }
  return mapping.value_minimum;
}

QtToolSettingsNumberSliderCurve adaptive_curve(double minimum, double maximum,
                                               double step) {
  const double span = maximum - minimum;
  if (!std::isfinite(step) || step <= 0.0 || span <= 0.0 ||
      span / step < kQtToolSettingsNonlinearStepSpanThreshold) {
    return QtToolSettingsNumberSliderCurve::Linear;
  }
  if (minimum >= 0.0) {
    return QtToolSettingsNumberSliderCurve::LogarithmicFromMinimum;
  }
  if (maximum <= 0.0) {
    return QtToolSettingsNumberSliderCurve::LogarithmicFromMaximum;
  }
  return QtToolSettingsNumberSliderCurve::SymmetricLogarithmic;
}

double adaptive_curve_scale(double span, double step) {
  if (!std::isfinite(step) || step <= 0.0 || !std::isfinite(span) ||
      span <= 0.0) {
    return 1.0;
  }
  // Keep at least one logical descriptor step in the near-zero linear region,
  // while ensuring a practical low value occupies visible slider travel even
  // when the descriptor spans many orders of magnitude.
  const double scale = std::max(
      step, span / static_cast<double>(kQtToolSettingsFloatingSliderResolution));
  return std::isfinite(scale) && scale > 0.0 ? scale : 1.0;
}

} // namespace

QtToolSettingsNumberSliderMapping
qt_tool_settings_number_slider_mapping(double minimum, double maximum,
                                       bool integer, double step) {
  const auto [value_minimum, value_maximum] =
      sanitized_bounds(minimum, maximum);
  if (integer) {
    const int slider_minimum = integer_bound(value_minimum);
    const int slider_maximum = integer_bound(value_maximum);
    return {
        .slider_minimum = slider_minimum,
        .slider_maximum = slider_maximum,
        .value_minimum = static_cast<double>(slider_minimum),
        .value_maximum = static_cast<double>(slider_maximum),
        .integer = true,
    };
  }
  const double span = value_maximum - value_minimum;
  const QtToolSettingsNumberSliderCurve curve =
      adaptive_curve(value_minimum, value_maximum, step);
  return {
      .slider_minimum = 0,
      .slider_maximum = value_minimum == value_maximum
                            ? 0
                            : kQtToolSettingsFloatingSliderResolution,
      .value_minimum = value_minimum,
      .value_maximum = value_maximum,
      .integer = false,
      .curve = curve,
      .curve_scale = adaptive_curve_scale(span, step),
  };
}

int qt_tool_settings_number_slider_position(
    const QtToolSettingsNumberSliderMapping &mapping, double value) {
  if (!std::isfinite(value)) {
    value = mapping.value_minimum;
  }
  if (mapping.integer) {
    return std::clamp(integer_bound(value), mapping.slider_minimum,
                      mapping.slider_maximum);
  }
  if (mapping.slider_maximum <= mapping.slider_minimum ||
      mapping.value_maximum <= mapping.value_minimum) {
    return mapping.slider_minimum;
  }
  const double clamped =
      std::clamp(value, mapping.value_minimum, mapping.value_maximum);
  if (clamped == mapping.value_minimum) {
    return mapping.slider_minimum;
  }
  if (clamped == mapping.value_maximum) {
    return mapping.slider_maximum;
  }
  const double normalized = normalized_slider_fraction(mapping, clamped);
  const double slider_span =
      static_cast<double>(mapping.slider_maximum - mapping.slider_minimum);
  return std::clamp(mapping.slider_minimum + static_cast<int>(std::llround(
                                                 normalized * slider_span)),
                    mapping.slider_minimum, mapping.slider_maximum);
}

double qt_tool_settings_number_slider_value(
    const QtToolSettingsNumberSliderMapping &mapping, int position) {
  const int clamped =
      std::clamp(position, mapping.slider_minimum, mapping.slider_maximum);
  if (mapping.integer) {
    return static_cast<double>(clamped);
  }
  if (mapping.slider_maximum <= mapping.slider_minimum ||
      mapping.value_maximum <= mapping.value_minimum) {
    return mapping.value_minimum;
  }
  const double normalized =
      static_cast<double>(clamped - mapping.slider_minimum) /
      static_cast<double>(mapping.slider_maximum - mapping.slider_minimum);
  if (clamped == mapping.slider_minimum) {
    return mapping.value_minimum;
  }
  if (clamped == mapping.slider_maximum) {
    return mapping.value_maximum;
  }
  return std::clamp(value_from_normalized_slider_fraction(mapping, normalized),
                    mapping.value_minimum, mapping.value_maximum);
}

int qt_tool_settings_number_slider_step(
    const QtToolSettingsNumberSliderMapping &mapping, double value,
    double logical_step) {
  if (mapping.integer) {
    if (!std::isfinite(logical_step) || logical_step <= 0.0) {
      return 1;
    }
    return std::max(1, integer_bound(logical_step));
  }
  if (!std::isfinite(logical_step) || logical_step <= 0.0 ||
      mapping.slider_maximum <= mapping.slider_minimum) {
    return 1;
  }
  const double clamped =
      std::clamp(value, mapping.value_minimum, mapping.value_maximum);
  const int current =
      qt_tool_settings_number_slider_position(mapping, clamped);
  const int lower = qt_tool_settings_number_slider_position(
      mapping, std::max(mapping.value_minimum, clamped - logical_step));
  const int upper = qt_tool_settings_number_slider_position(
      mapping, std::min(mapping.value_maximum, clamped + logical_step));
  return std::max({1, std::abs(current - lower), std::abs(upper - current)});
}

QtToolSettingsNumberSlider::QtToolSettingsNumberSlider(
    Qt::Orientation orientation, QWidget *parent)
    : QSlider(orientation, parent) {}

int QtToolSettingsNumberSlider::handle_center_offset() const {
  QStyleOptionSlider option;
  initStyleOption(&option);
  const QRect handle = style()->subControlRect(
      QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);
  return orientation() == Qt::Horizontal
             ? handle.center().x() - handle.left()
             : handle.center().y() - handle.top();
}

int QtToolSettingsNumberSlider::value_from_pointer(
    const QPointF &position) const {
  QStyleOptionSlider option;
  initStyleOption(&option);
  const QRect groove = style()->subControlRect(
      QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
  const QRect handle = style()->subControlRect(
      QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

  const bool horizontal = orientation() == Qt::Horizontal;
  const int handle_length = horizontal ? handle.width() : handle.height();
  const int slider_minimum = horizontal ? groove.left() : groove.top();
  const int slider_maximum =
      (horizontal ? groove.right() : groove.bottom()) - handle_length + 1;
  const int pointer =
      static_cast<int>(std::lround(horizontal ? position.x() : position.y())) -
      handle_center_offset();
  return QStyle::sliderValueFromPosition(
      minimum(), maximum(), pointer - slider_minimum,
      std::max(0, slider_maximum - slider_minimum), option.upsideDown);
}

void QtToolSettingsNumberSlider::mousePressEvent(QMouseEvent *event) {
  if (event->button() != Qt::LeftButton || minimum() == maximum()) {
    QSlider::mousePressEvent(event);
    return;
  }

  QStyleOptionSlider option;
  initStyleOption(&option);
  const QStyle::SubControl control = style()->hitTestComplexControl(
      QStyle::CC_Slider, &option, event->position().toPoint(), this);
  if (control != QStyle::SC_SliderGroove) {
    QSlider::mousePressEvent(event);
    return;
  }

  groove_scrubbing_ = true;
  setFocus(Qt::MouseFocusReason);
  setSliderDown(true);
  setSliderPosition(value_from_pointer(event->position()));
  event->accept();
}

void QtToolSettingsNumberSlider::mouseMoveEvent(QMouseEvent *event) {
  if (!groove_scrubbing_) {
    QSlider::mouseMoveEvent(event);
    return;
  }
  if (!(event->buttons() & Qt::LeftButton)) {
    finish_groove_scrub();
    event->accept();
    return;
  }
  setSliderPosition(value_from_pointer(event->position()));
  event->accept();
}

void QtToolSettingsNumberSlider::mouseReleaseEvent(QMouseEvent *event) {
  if (!groove_scrubbing_ || event->button() != Qt::LeftButton) {
    QSlider::mouseReleaseEvent(event);
    return;
  }
  setSliderPosition(value_from_pointer(event->position()));
  finish_groove_scrub();
  event->accept();
}

void QtToolSettingsNumberSlider::finish_groove_scrub() {
  if (!groove_scrubbing_) {
    return;
  }
  groove_scrubbing_ = false;
  setSliderDown(false);
}

} // namespace quader
