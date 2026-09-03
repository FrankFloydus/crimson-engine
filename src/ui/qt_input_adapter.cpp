////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_input_adapter.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QWheelEvent>

#include <cmath>
#include <cstdlib>

namespace quader {
namespace {

struct WheelDelta {
  QPoint value;
  QtWheelDeltaSource source = QtWheelDeltaSource::None;
};

bool has_modifier(Qt::KeyboardModifiers modifiers,
                  Qt::KeyboardModifier modifier) {
  return modifiers.testFlag(modifier);
}

QuaderInputChord keyboard_chord(QuaderKeyCode key,
                                Qt::KeyboardModifiers modifiers) {
  return {
      .primary = has_modifier(modifiers, Qt::ControlModifier) ||
                 has_modifier(modifiers, Qt::MetaModifier),
      .shift = has_modifier(modifiers, Qt::ShiftModifier),
      .alt = has_modifier(modifiers, Qt::AltModifier),
      .device = QuaderInputDevice::Keyboard,
      .key = key,
  };
}

QuaderInputChord mouse_chord(QuaderMouseButton button,
                             QuaderInputGesture gesture,
                             Qt::KeyboardModifiers modifiers) {
  return {
      .primary = has_modifier(modifiers, Qt::ControlModifier) ||
                 has_modifier(modifiers, Qt::MetaModifier),
      .shift = has_modifier(modifiers, Qt::ShiftModifier),
      .alt = has_modifier(modifiers, Qt::AltModifier),
      .device = QuaderInputDevice::Mouse,
      .mouse_button = button,
      .gesture = gesture,
  };
}

bool mouse_event_matches_gesture(QEvent::Type type,
                                 QuaderInputGesture gesture) {
  switch (gesture) {
  case QuaderInputGesture::Press:
  case QuaderInputGesture::Drag:
    return type == QEvent::MouseButtonPress;
  case QuaderInputGesture::Click:
    return type == QEvent::MouseButtonRelease;
  case QuaderInputGesture::DoubleClick:
    return type == QEvent::MouseButtonDblClick;
  case QuaderInputGesture::Wheel:
  case QuaderInputGesture::WheelUp:
  case QuaderInputGesture::WheelDown:
  case QuaderInputGesture::WheelLeft:
  case QuaderInputGesture::WheelRight:
    return false;
  }
  return false;
}

std::optional<QuaderInputGesture> directional_wheel_gesture(QPoint delta) {
  if (delta.isNull()) {
    return std::nullopt;
  }

  if (std::abs(delta.y()) >= std::abs(delta.x()) && delta.y() != 0) {
    return delta.y() > 0 ? QuaderInputGesture::WheelUp
                         : QuaderInputGesture::WheelDown;
  }
  if (delta.x() != 0) {
    return delta.x() > 0 ? QuaderInputGesture::WheelRight
                         : QuaderInputGesture::WheelLeft;
  }
  return std::nullopt;
}

WheelDelta wheel_delta(const QWheelEvent &event) {
  const QPoint pixel = event.pixelDelta();
  const QPoint angle = event.angleDelta();
  WheelDelta result{
      .value = pixel.isNull() ? angle : pixel,
      .source = pixel.isNull() ? QtWheelDeltaSource::Angle
                               : QtWheelDeltaSource::Pixel,
  };
  if (result.value.isNull()) {
    result.source = QtWheelDeltaSource::None;
  }
#ifdef _WIN32
  // Qt's Windows pointer handler represents a vertical wheel as horizontal
  // whenever Alt is held. Restore the physical direction before canonical
  // gesture classification so Alt+WheelUp/Down remains bindable. A genuine
  // horizontal wheel cannot be distinguished after that QPA rewrite, and the
  // Alt-modified vertical binding deliberately owns this representation.
  if (event.modifiers().testFlag(Qt::AltModifier) && result.value.y() == 0 &&
      result.value.x() != 0) {
    result.value = {0, result.value.x()};
  }
#endif
  return result;
}

void append_wheel_event(QtWheelInputTranslation &translation,
                        const QuaderInputChord &directional,
                        double step_delta, QuaderInputPosition local) {
  const bool vertical =
      directional.gesture == QuaderInputGesture::WheelUp ||
      directional.gesture == QuaderInputGesture::WheelDown;
  if (vertical) {
    translation.mouse_wheel += static_cast<float>(step_delta);
    QuaderInputChord generic = directional;
    generic.gesture = QuaderInputGesture::Wheel;
    translation.wheel_input_edges.push_back({
        .chord = generic,
        .step_delta = step_delta,
        .local = local,
        .has_local_position = true,
    });
  }
  translation.wheel_input_edges.push_back({
      .chord = directional,
      .step_delta = step_delta,
      .local = local,
      .has_local_position = true,
  });
}

QuaderInputPosition wheel_local_position(const QWheelEvent &event,
                                         double coordinate_scale) {
  const double safe_scale =
      std::isfinite(coordinate_scale) && coordinate_scale > 0.0
          ? coordinate_scale
          : 1.0;
  return {
      .x = static_cast<float>(event.position().x() * safe_scale),
      .y = static_cast<float>(event.position().y() * safe_scale),
  };
}

bool opposite_sign(int left, int right) {
  return (left < 0 && right > 0) || (left > 0 && right < 0);
}

QtWheelInputAccumulator::Axis wheel_axis(QuaderInputGesture gesture) {
  if (gesture == QuaderInputGesture::WheelUp ||
      gesture == QuaderInputGesture::WheelDown) {
    return QtWheelInputAccumulator::Axis::Vertical;
  }
  return QtWheelInputAccumulator::Axis::Horizontal;
}

} // namespace

std::optional<QuaderKeyCode>
quader_key_code_from_qt_key(int key, Qt::KeyboardModifiers modifiers) {
  if (key >= Qt::Key_A && key <= Qt::Key_Z) {
    return static_cast<QuaderKeyCode>(
        static_cast<int>(QuaderKeyCode::A) + (key - Qt::Key_A));
  }

  const bool keypad = has_modifier(modifiers, Qt::KeypadModifier);
  if (key >= Qt::Key_0 && key <= Qt::Key_9) {
    const QuaderKeyCode first =
        keypad ? QuaderKeyCode::Numpad0 : QuaderKeyCode::Digit0;
    return static_cast<QuaderKeyCode>(static_cast<int>(first) +
                                      (key - Qt::Key_0));
  }

  if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
    return static_cast<QuaderKeyCode>(
        static_cast<int>(QuaderKeyCode::F1) + (key - Qt::Key_F1));
  }

  if (keypad) {
    switch (key) {
    case Qt::Key_Plus:
      return QuaderKeyCode::NumpadAdd;
    case Qt::Key_Minus:
      return QuaderKeyCode::NumpadSubtract;
    case Qt::Key_Asterisk:
      return QuaderKeyCode::NumpadMultiply;
    case Qt::Key_Slash:
      return QuaderKeyCode::NumpadDivide;
    case Qt::Key_Period:
    case Qt::Key_Comma:
      return QuaderKeyCode::NumpadDecimal;
    default:
      break;
    }
  }

  if (has_modifier(modifiers, Qt::ShiftModifier)) {
    switch (key) {
    case Qt::Key_ParenRight:
      return QuaderKeyCode::Digit0;
    case Qt::Key_Exclam:
      return QuaderKeyCode::Digit1;
    case Qt::Key_At:
      return QuaderKeyCode::Digit2;
    case Qt::Key_NumberSign:
      return QuaderKeyCode::Digit3;
    case Qt::Key_Dollar:
      return QuaderKeyCode::Digit4;
    case Qt::Key_Percent:
      return QuaderKeyCode::Digit5;
    case Qt::Key_AsciiCircum:
      return QuaderKeyCode::Digit6;
    case Qt::Key_Ampersand:
      return QuaderKeyCode::Digit7;
    case Qt::Key_Asterisk:
      return QuaderKeyCode::Digit8;
    case Qt::Key_ParenLeft:
      return QuaderKeyCode::Digit9;
    case Qt::Key_Less:
      return QuaderKeyCode::Comma;
    case Qt::Key_Greater:
      return QuaderKeyCode::Period;
    case Qt::Key_Question:
      return QuaderKeyCode::Slash;
    case Qt::Key_Bar:
      return QuaderKeyCode::Backslash;
    case Qt::Key_Colon:
      return QuaderKeyCode::Semicolon;
    case Qt::Key_QuoteDbl:
      return QuaderKeyCode::Apostrophe;
    case Qt::Key_BraceLeft:
      return QuaderKeyCode::LeftBracket;
    case Qt::Key_BraceRight:
      return QuaderKeyCode::RightBracket;
    case Qt::Key_Underscore:
      return QuaderKeyCode::Minus;
    case Qt::Key_Plus:
      return QuaderKeyCode::Equal;
    case Qt::Key_AsciiTilde:
      return QuaderKeyCode::Grave;
    default:
      break;
    }
  }

  switch (key) {
  case Qt::Key_Comma:
    return QuaderKeyCode::Comma;
  case Qt::Key_Period:
    return QuaderKeyCode::Period;
  case Qt::Key_Slash:
    return QuaderKeyCode::Slash;
  case Qt::Key_Backslash:
    return QuaderKeyCode::Backslash;
  case Qt::Key_Semicolon:
    return QuaderKeyCode::Semicolon;
  case Qt::Key_Apostrophe:
    return QuaderKeyCode::Apostrophe;
  case Qt::Key_BracketLeft:
    return QuaderKeyCode::LeftBracket;
  case Qt::Key_BracketRight:
    return QuaderKeyCode::RightBracket;
  case Qt::Key_Minus:
    return QuaderKeyCode::Minus;
  case Qt::Key_Equal:
    return QuaderKeyCode::Equal;
  case Qt::Key_QuoteLeft:
    return QuaderKeyCode::Grave;
  case Qt::Key_Escape:
    return QuaderKeyCode::Escape;
  case Qt::Key_Tab:
  case Qt::Key_Backtab:
    return QuaderKeyCode::Tab;
  case Qt::Key_Backspace:
    return QuaderKeyCode::Backspace;
  case Qt::Key_Return:
    return QuaderKeyCode::Enter;
  case Qt::Key_Enter:
    return QuaderKeyCode::NumpadEnter;
  case Qt::Key_Space:
    return QuaderKeyCode::Space;
  case Qt::Key_Insert:
    return QuaderKeyCode::Insert;
  case Qt::Key_Delete:
    return QuaderKeyCode::Delete;
  case Qt::Key_Home:
    return QuaderKeyCode::Home;
  case Qt::Key_End:
    return QuaderKeyCode::End;
  case Qt::Key_PageUp:
    return QuaderKeyCode::PageUp;
  case Qt::Key_PageDown:
    return QuaderKeyCode::PageDown;
  case Qt::Key_Left:
    return QuaderKeyCode::Left;
  case Qt::Key_Right:
    return QuaderKeyCode::Right;
  case Qt::Key_Up:
    return QuaderKeyCode::Up;
  case Qt::Key_Down:
    return QuaderKeyCode::Down;
  default:
    return std::nullopt;
  }
}

QuaderInputChord
quader_input_chord_from_qt_key(int key, Qt::KeyboardModifiers modifiers) {
  const std::optional<QuaderKeyCode> key_code =
      quader_key_code_from_qt_key(key, modifiers);
  return key_code.has_value() ? keyboard_chord(*key_code, modifiers)
                              : QuaderInputChord{};
}

std::optional<QuaderInputChord>
quader_pressed_input_chord_from_qt_event(const QKeyEvent &event) {
  if (event.type() != QEvent::KeyPress || event.isAutoRepeat()) {
    return std::nullopt;
  }

  QuaderInputChord chord =
      quader_input_chord_from_qt_key(event.key(), event.modifiers());
  return chord.empty() ? std::nullopt
                       : std::optional<QuaderInputChord>(chord);
}

std::optional<QuaderMouseButton>
quader_mouse_button_from_qt_button(Qt::MouseButton button) {
  switch (button) {
  case Qt::LeftButton:
    return QuaderMouseButton::Left;
  case Qt::MiddleButton:
    return QuaderMouseButton::Middle;
  case Qt::RightButton:
    return QuaderMouseButton::Right;
  case Qt::BackButton:
    return QuaderMouseButton::Back;
  case Qt::ForwardButton:
    return QuaderMouseButton::Forward;
  case Qt::ExtraButton3:
    return QuaderMouseButton::Button6;
  case Qt::ExtraButton4:
    return QuaderMouseButton::Button7;
  case Qt::ExtraButton5:
    return QuaderMouseButton::Button8;
  default:
    return std::nullopt;
  }
}

std::optional<QuaderInputChord>
quader_mouse_input_chord_from_qt_event(const QMouseEvent &event,
                                       QuaderInputGesture gesture) {
  if (!mouse_event_matches_gesture(event.type(), gesture)) {
    return std::nullopt;
  }
  const std::optional<QuaderMouseButton> button =
      quader_mouse_button_from_qt_button(event.button());
  if (!button.has_value()) {
    return std::nullopt;
  }
  return mouse_chord(*button, gesture, event.modifiers());
}

std::optional<QuaderInputChord>
quader_wheel_input_chord_from_qt_event(const QWheelEvent &event) {
  const std::optional<QuaderInputGesture> gesture =
      directional_wheel_gesture(wheel_delta(event).value);
  if (!gesture.has_value()) {
    return std::nullopt;
  }
  return mouse_chord(QuaderMouseButton::None, *gesture, event.modifiers());
}

QtWheelInputTranslation
quader_wheel_input_from_qt_event(const QWheelEvent &event) {
  QtWheelInputTranslation translation;
  const std::optional<QuaderInputChord> directional =
      quader_wheel_input_chord_from_qt_event(event);
  if (!directional.has_value()) {
    return translation;
  }

  const WheelDelta delta = wheel_delta(event);
  translation.delta_source = delta.source;
  const bool vertical =
      directional->gesture == QuaderInputGesture::WheelUp ||
      directional->gesture == QuaderInputGesture::WheelDown;
  translation.source_delta = vertical ? delta.value.y() : delta.value.x();
  const double units_per_step =
      delta.source == QtWheelDeltaSource::Pixel
          ? kQtWheelPixelsPerStep
          : static_cast<double>(kQtWheelAngleUnitsPerStep);
  const double step_delta =
      static_cast<double>(translation.source_delta) / units_per_step;
  append_wheel_event(translation, *directional, step_delta,
                     wheel_local_position(event, 1.0f));
  return translation;
}

QtWheelInputTranslation quader_wheel_input_from_qt_event(
    const QWheelEvent &event, QtWheelInputAccumulator &accumulator,
    double local_coordinate_scale) {
  QtWheelInputTranslation translation;
  const std::optional<QuaderInputChord> directional =
      quader_wheel_input_chord_from_qt_event(event);
  if (!directional.has_value()) {
    return translation;
  }

  const WheelDelta delta = wheel_delta(event);
  translation.delta_source = delta.source;
  const bool vertical =
      directional->gesture == QuaderInputGesture::WheelUp ||
      directional->gesture == QuaderInputGesture::WheelDown;
  translation.source_delta = vertical ? delta.value.y() : delta.value.x();

  if (delta.source == QtWheelDeltaSource::Pixel) {
    accumulator = {};
    append_wheel_event(
        translation, *directional,
        static_cast<double>(translation.source_delta) /
            kQtWheelPixelsPerStep,
        wheel_local_position(event, local_coordinate_scale));
    return translation;
  }

  const QtWheelInputAccumulator::Axis axis =
      wheel_axis(directional->gesture);
  if (accumulator.angle_delta_axis != axis ||
      opposite_sign(accumulator.angle_delta_remainder,
                    translation.source_delta)) {
    accumulator.angle_delta_remainder = 0;
  }
  accumulator.angle_delta_axis = axis;
  accumulator.angle_delta_remainder += translation.source_delta;

  const int whole_steps =
      accumulator.angle_delta_remainder / kQtWheelAngleUnitsPerStep;
  accumulator.angle_delta_remainder -=
      whole_steps * kQtWheelAngleUnitsPerStep;
  const double signed_step = whole_steps < 0 ? -1.0 : 1.0;
  for (int index = 0; index < std::abs(whole_steps); ++index) {
    append_wheel_event(translation, *directional, signed_step,
                       wheel_local_position(event, local_coordinate_scale));
  }
  return translation;
}

} // namespace quader
