////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"

#include <QtCore/Qt>

#include <optional>
#include <vector>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

namespace quader {

/**
 * Stores the semantic input produced by one Qt wheel event. Horizontal wheel
 * events intentionally carry only their directional binding chord; vertical
 * events also carry generic-wheel and zoom input.
 */
struct QtWheelInputTranslation {
  float mouse_wheel = 0.0f;
  std::vector<QuaderInputChord> wheel_input_chords;
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
 * Builds a canonical Press, DoubleClick, or Drag chord from a matching Qt
 * mouse event. A Drag chord is intentionally built from the button-press
 * event so callers can retain its event-time modifiers for the whole drag.
 */
[[nodiscard]] std::optional<QuaderInputChord>
quader_mouse_input_chord_from_qt_event(const QMouseEvent &event,
                                       QuaderInputGesture gesture);

/**
 * Converts a Qt wheel event into one directional canonical wheel chord.
 * Smooth-scroll pixel deltas are used when no angle delta is available.
 */
[[nodiscard]] std::optional<QuaderInputChord>
quader_wheel_input_chord_from_qt_event(const QWheelEvent &event);

/**
 * Converts one Qt wheel event into its complete semantic viewport input.
 */
[[nodiscard]] QtWheelInputTranslation
quader_wheel_input_from_qt_event(const QWheelEvent &event);

} // namespace quader
