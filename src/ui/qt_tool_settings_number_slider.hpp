////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <QSlider>

#include <cstdint>

namespace quader {

inline constexpr int kQtToolSettingsFloatingSliderResolution = 100'000;
inline constexpr double kQtToolSettingsNonlinearStepSpanThreshold = 65'536.0;

/**
 * Selects the logical curve used between a descriptor value and QSlider's
 * integral position. Wide floating ranges use a logarithmic curve around the
 * nearest zero boundary; ordinary ranges remain linear.
 */
enum class QtToolSettingsNumberSliderCurve : std::uint8_t {
  Linear,
  LogarithmicFromMinimum,
  LogarithmicFromMaximum,
  SymmetricLogarithmic,
};

/**
 * Maps one descriptor-owned numeric range onto the integral range required by
 * QSlider. Integer descriptors retain their exact native range, while floating
 * descriptors use a bounded linear or adaptive logarithmic range.
 */
struct QtToolSettingsNumberSliderMapping {
  int slider_minimum = 0;
  int slider_maximum = 0;
  double value_minimum = 0.0;
  double value_maximum = 0.0;
  bool integer = false;
  QtToolSettingsNumberSliderCurve curve =
      QtToolSettingsNumberSliderCurve::Linear;
  double curve_scale = 1.0;

  bool operator==(const QtToolSettingsNumberSliderMapping &) const = default;
};

[[nodiscard]] QtToolSettingsNumberSliderMapping
qt_tool_settings_number_slider_mapping(double minimum, double maximum,
                                       bool integer, double step = 0.0);

[[nodiscard]] int qt_tool_settings_number_slider_position(
    const QtToolSettingsNumberSliderMapping &mapping, double value);

[[nodiscard]] double qt_tool_settings_number_slider_value(
    const QtToolSettingsNumberSliderMapping &mapping, int position);

[[nodiscard]] int qt_tool_settings_number_slider_step(
    const QtToolSettingsNumberSliderMapping &mapping, double value,
    double logical_step);

/**
 * Keeps native handle dragging while making a left press on the slider groove
 * begin an absolute, continuously tracked scrub. Stock QSlider page-steps on
 * many styles when the groove is pressed and ignores subsequent mouse moves.
 */
class QtToolSettingsNumberSlider final : public QSlider {
public:
  explicit QtToolSettingsNumberSlider(
      Qt::Orientation orientation = Qt::Horizontal,
      QWidget *parent = nullptr);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  [[nodiscard]] int value_from_pointer(const QPointF &position) const;
  [[nodiscard]] int handle_center_offset() const;
  void finish_groove_scrub();

  bool groove_scrubbing_ = false;
};

} // namespace quader
