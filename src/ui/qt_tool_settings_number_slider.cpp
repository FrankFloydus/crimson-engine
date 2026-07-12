////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_tool_settings_number_slider.hpp"

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

} // namespace

QtToolSettingsNumberSliderMapping
qt_tool_settings_number_slider_mapping(double minimum, double maximum,
                                       bool integer) {
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
  return {
      .slider_minimum = 0,
      .slider_maximum = value_minimum == value_maximum
                            ? 0
                            : kQtToolSettingsFloatingSliderResolution,
      .value_minimum = value_minimum,
      .value_maximum = value_maximum,
      .integer = false,
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
  const double normalized = (clamped - mapping.value_minimum) /
                            (mapping.value_maximum - mapping.value_minimum);
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
  return mapping.value_minimum +
         (mapping.value_maximum - mapping.value_minimum) * normalized;
}

} // namespace quader
