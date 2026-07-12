////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_tool_settings_number_slider.hpp"

#include <array>

TEST(QtToolSettingsNumberSliderTests, IntegerRangesMapExactlyInBothDirections) {
  const quader::QtToolSettingsNumberSliderMapping mapping =
      quader::qt_tool_settings_number_slider_mapping(-12.0, 25.0, true);

  EXPECT_TRUE(mapping.integer);
  EXPECT_EQ(mapping.slider_minimum, -12);
  EXPECT_EQ(mapping.slider_maximum, 25);
  for (int value = mapping.slider_minimum; value <= mapping.slider_maximum;
       ++value) {
    EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, value),
              value);
    EXPECT_DOUBLE_EQ(
        quader::qt_tool_settings_number_slider_value(mapping, value), value);
  }
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, -100.0),
            -12);
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, 100.0),
            25);
}

TEST(QtToolSettingsNumberSliderTests,
     FloatingRangesUseBoundedNormalizedEndpointsAndMidpoint) {
  const quader::QtToolSettingsNumberSliderMapping mapping =
      quader::qt_tool_settings_number_slider_mapping(-2.5, 7.5, false);

  EXPECT_FALSE(mapping.integer);
  EXPECT_EQ(mapping.slider_minimum, 0);
  EXPECT_EQ(mapping.slider_maximum,
            quader::kQtToolSettingsFloatingSliderResolution);
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, -2.5), 0);
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, 2.5),
            quader::kQtToolSettingsFloatingSliderResolution / 2);
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, 7.5),
            quader::kQtToolSettingsFloatingSliderResolution);
  EXPECT_DOUBLE_EQ(quader::qt_tool_settings_number_slider_value(mapping, 0),
                   -2.5);
  EXPECT_DOUBLE_EQ(
      quader::qt_tool_settings_number_slider_value(
          mapping, quader::kQtToolSettingsFloatingSliderResolution / 2),
      2.5);
  EXPECT_DOUBLE_EQ(
      quader::qt_tool_settings_number_slider_value(
          mapping, quader::kQtToolSettingsFloatingSliderResolution),
      7.5);
}

TEST(QtToolSettingsNumberSliderTests,
     FloatingRoundTripsStayWithinHalfOneNormalizedStep) {
  const quader::QtToolSettingsNumberSliderMapping mapping =
      quader::qt_tool_settings_number_slider_mapping(0.0625, 16384.0, false);
  constexpr std::array<double, 6> kValues{0.0625, 0.25,   1.0,
                                          127.75, 8192.0, 16384.0};
  const double tolerance =
      (mapping.value_maximum - mapping.value_minimum) /
          static_cast<double>(quader::kQtToolSettingsFloatingSliderResolution) /
          2.0 +
      0.000001;

  for (double value : kValues) {
    const int position =
        quader::qt_tool_settings_number_slider_position(mapping, value);
    const double round_trip =
        quader::qt_tool_settings_number_slider_value(mapping, position);
    EXPECT_NEAR(round_trip, value, tolerance);
  }
}

TEST(QtToolSettingsNumberSliderTests,
     DegenerateAndReversedRangesRemainDeterministic) {
  const quader::QtToolSettingsNumberSliderMapping reversed =
      quader::qt_tool_settings_number_slider_mapping(5.0, -5.0, false);
  EXPECT_DOUBLE_EQ(reversed.value_minimum, -5.0);
  EXPECT_DOUBLE_EQ(reversed.value_maximum, 5.0);

  const quader::QtToolSettingsNumberSliderMapping degenerate =
      quader::qt_tool_settings_number_slider_mapping(3.0, 3.0, false);
  EXPECT_EQ(degenerate.slider_minimum, 0);
  EXPECT_EQ(degenerate.slider_maximum, 0);
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(degenerate, 99.0),
            0);
  EXPECT_DOUBLE_EQ(quader::qt_tool_settings_number_slider_value(degenerate, 99),
                   3.0);
}
