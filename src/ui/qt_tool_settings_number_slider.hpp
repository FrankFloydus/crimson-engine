////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

inline constexpr int kQtToolSettingsFloatingSliderResolution = 10'000;

/**
 * Maps one descriptor-owned numeric range onto the integral range required by
 * QSlider. Integer descriptors retain their exact native range, while floating
 * descriptors use a bounded normalized range.
 */
struct QtToolSettingsNumberSliderMapping {
  int slider_minimum = 0;
  int slider_maximum = 0;
  double value_minimum = 0.0;
  double value_maximum = 0.0;
  bool integer = false;
};

[[nodiscard]] QtToolSettingsNumberSliderMapping
qt_tool_settings_number_slider_mapping(double minimum, double maximum,
                                       bool integer);

[[nodiscard]] int qt_tool_settings_number_slider_position(
    const QtToolSettingsNumberSliderMapping &mapping, double value);

[[nodiscard]] double qt_tool_settings_number_slider_value(
    const QtToolSettingsNumberSliderMapping &mapping, int position);

} // namespace quader
