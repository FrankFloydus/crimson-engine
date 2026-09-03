////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"

#include <QtCore/Qt>

#include <cstdint>
#include <optional>
#include <vector>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace quader {

/// Identifies which Qt wheel payload supplied the canonical motion.
enum class QtWheelDeltaSource : std::uint8_t {
  None,
  Angle,
  Pixel,
};

/// Qt specifies one conventional wheel step as 120 angle-delta units.
inline constexpr int kQtWheelAngleUnitsPerStep = 120;

/**
 * Calibrates continuous touchpad scrolling independently from angle units.
 * Forty logical pixels is one viewport zoom step; unlike an angle wheel,
 * pixel motion is intentionally not quantized.
 */
inline constexpr double kQtWheelPixelsPerStep = 40.0;

/**
 * Retains high-resolution angle-wheel motion for one input surface. A change
 * of axis or direction clears an incomplete detent so stale motion cannot
 * weaken the first notch after the user reverses direction.
 */
struct QtWheelInputAccumulator {
  /// Identifies the angle-wheel axis whose incomplete detent is retained.
  enum class Axis : std::uint8_t {
    None,
    Horizontal,
    Vertical,
  };

  int angle_delta_remainder = 0;
  Axis angle_delta_axis = Axis::None;
};

/**
 * Stores the semantic input produced by one Qt wheel event. Horizontal wheel
 * events intentionally carry only their directional binding chord; vertical
 * events also carry generic-wheel and zoom input.
 */
struct QtWheelInputTranslation {
  float mouse_wheel = 0.0f;
  std::vector<QuaderWheelInputEdge> wheel_input_edges;
  QtWheelDeltaSource delta_source = QtWheelDeltaSource::None;
  int source_delta = 0;
};

/**
 * Converts one Qt keyboard key into Quader's canonical key vocabulary.
 * KeypadModifier distinguishes keypad digits and operators from their main
 * keyboard equivalents.
 */
[[nodiscard]] std::optional<QuaderKeyCode>
quader_key_code_from_qt_key(int key, Qt::KeyboardModifiers modifiers);

/**
 * Builds one canonical keyboard chord using the modifiers captured by Qt for
 * the same event.
 */
[[nodiscard]] QuaderInputChord
quader_input_chord_from_qt_key(int key, Qt::KeyboardModifiers modifiers);

/**
 * Converts a non-repeating Qt key-press event into a canonical input chord.
 */
[[nodiscard]] std::optional<QuaderInputChord>
quader_pressed_input_chord_from_qt_event(const QKeyEvent &event);

/**
 * Converts one Qt mouse button into Quader's canonical mouse vocabulary.
 */
[[nodiscard]] std::optional<QuaderMouseButton>
quader_mouse_button_from_qt_button(Qt::MouseButton button);

/**
 * Builds a canonical Press, Click, DoubleClick, or Drag chord from a matching
 * Qt mouse event. Click consumes a button-release event. A Drag chord is
 * intentionally built from the button-press event so callers can retain its
 * event-time modifiers for the whole drag.
 */
[[nodiscard]] std::optional<QuaderInputChord>
quader_mouse_input_chord_from_qt_event(const QMouseEvent &event,
                                       QuaderInputGesture gesture);

/**
 * Converts a Qt wheel event into one directional canonical wheel chord.
 * Smooth-scroll pixel deltas take precedence when Qt provides both payloads.
 */
[[nodiscard]] std::optional<QuaderInputChord>
quader_wheel_input_chord_from_qt_event(const QWheelEvent &event);

/**
 * Converts one Qt wheel event without retained state. This compatibility path
 * keeps angle motion continuous for consumers such as material previews.
 */
[[nodiscard]] QtWheelInputTranslation
quader_wheel_input_from_qt_event(const QWheelEvent &event);

/**
 * Converts one Qt wheel event using persistent per-surface angle-wheel state.
 * Complete angle detents are emitted as ordered integral +/-1 events. Pixel
 * deltas bypass that detent accumulator and remain continuous. The coordinate
 * scale maps Qt's logical event position into the consumer's local units.
 */
[[nodiscard]] QtWheelInputTranslation quader_wheel_input_from_qt_event(
    const QWheelEvent &event, QtWheelInputAccumulator &accumulator,
    double local_coordinate_scale = 1.0);

} // namespace quader
