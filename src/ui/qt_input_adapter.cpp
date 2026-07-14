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

#include <cstdlib>

namespace quader {
namespace {

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

QPoint wheel_delta(const QWheelEvent &event) {
  const QPoint angle = event.angleDelta();
  return angle.isNull() ? event.pixelDelta() : angle;
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
      directional_wheel_gesture(wheel_delta(event));
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

  const QPoint delta = wheel_delta(event);
  const bool vertical =
      directional->gesture == QuaderInputGesture::WheelUp ||
      directional->gesture == QuaderInputGesture::WheelDown;
  const double step_delta =
      static_cast<double>(vertical ? delta.y() : delta.x()) / 120.0;
  if (vertical) {
    translation.mouse_wheel = static_cast<float>(step_delta);
    QuaderInputChord generic = *directional;
    generic.gesture = QuaderInputGesture::Wheel;
    translation.wheel_input_edges.push_back({
        .chord = generic,
        .step_delta = step_delta,
    });
  }
  translation.wheel_input_edges.push_back({
      .chord = *directional,
      .step_delta = step_delta,
  });
  return translation;
}

} // namespace quader
