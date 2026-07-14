////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_input_adapter.hpp"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QWheelEvent>

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace {

using quader::QuaderInputChord;
using quader::QuaderInputDevice;
using quader::QuaderInputGesture;
using quader::QuaderKeyCode;
using quader::QuaderMouseButton;
using quader::quader_input_chord_from_qt_key;
using quader::quader_key_code_from_qt_key;
using quader::quader_mouse_button_from_qt_button;
using quader::quader_mouse_input_chord_from_qt_event;
using quader::quader_pressed_input_chord_from_qt_event;
using quader::quader_wheel_input_chord_from_qt_event;
using quader::quader_wheel_input_from_qt_event;

/// Defines one Qt key to canonical Quader key conversion expectation.
struct KeyMapping {
  int qt_key = Qt::Key_unknown;
  QuaderKeyCode quader_key = QuaderKeyCode::None;
};

QMouseEvent mouse_event(QEvent::Type type, Qt::MouseButton button,
                        Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  return QMouseEvent(type, QPointF{12.0, 24.0}, QPointF{112.0, 224.0},
                     button, button, modifiers);
}

QWheelEvent wheel_event(QPoint pixel_delta, QPoint angle_delta,
                        Qt::KeyboardModifiers modifiers = Qt::NoModifier,
                        bool inverted = false) {
  return QWheelEvent(QPointF{12.0, 24.0}, QPointF{112.0, 224.0},
                     pixel_delta, angle_delta, Qt::NoButton, modifiers,
                     Qt::NoScrollPhase, inverted);
}

void expect_mouse_chord(const QuaderInputChord &chord,
                        QuaderMouseButton button,
                        QuaderInputGesture gesture) {
  EXPECT_EQ(chord.device, QuaderInputDevice::Mouse);
  EXPECT_EQ(chord.mouse_button, button);
  EXPECT_EQ(chord.gesture, gesture);
}

} // namespace

TEST(QtInputAdapterTests, MapsEveryLetterAndTopRowDigit) {
  for (int offset = 0; offset < 26; ++offset) {
    SCOPED_TRACE(offset);
    const std::optional<QuaderKeyCode> key = quader_key_code_from_qt_key(
        Qt::Key_A + offset, Qt::NoModifier);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, static_cast<QuaderKeyCode>(
                        static_cast<int>(QuaderKeyCode::A) + offset));
  }

  for (int offset = 0; offset < 10; ++offset) {
    SCOPED_TRACE(offset);
    const std::optional<QuaderKeyCode> key = quader_key_code_from_qt_key(
        Qt::Key_0 + offset, Qt::NoModifier);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, static_cast<QuaderKeyCode>(
                        static_cast<int>(QuaderKeyCode::Digit0) + offset));
  }
}

TEST(QtInputAdapterTests, MapsNumpadDigitsAndOperators) {
  for (int offset = 0; offset < 10; ++offset) {
    SCOPED_TRACE(offset);
    const std::optional<QuaderKeyCode> key = quader_key_code_from_qt_key(
        Qt::Key_0 + offset, Qt::KeypadModifier);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, static_cast<QuaderKeyCode>(
                        static_cast<int>(QuaderKeyCode::Numpad0) + offset));
  }

  constexpr std::array kMappings{
      KeyMapping{Qt::Key_Plus, QuaderKeyCode::NumpadAdd},
      KeyMapping{Qt::Key_Minus, QuaderKeyCode::NumpadSubtract},
      KeyMapping{Qt::Key_Asterisk, QuaderKeyCode::NumpadMultiply},
      KeyMapping{Qt::Key_Slash, QuaderKeyCode::NumpadDivide},
      KeyMapping{Qt::Key_Period, QuaderKeyCode::NumpadDecimal},
      KeyMapping{Qt::Key_Comma, QuaderKeyCode::NumpadDecimal},
      KeyMapping{Qt::Key_Enter, QuaderKeyCode::NumpadEnter},
  };
  for (const KeyMapping &mapping : kMappings) {
    SCOPED_TRACE(mapping.qt_key);
    const std::optional<QuaderKeyCode> key = quader_key_code_from_qt_key(
        mapping.qt_key, Qt::KeypadModifier);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, mapping.quader_key);
  }
}

TEST(QtInputAdapterTests, MapsPunctuationNavigationAndEditingKeys) {
  constexpr std::array kMappings{
      KeyMapping{Qt::Key_Comma, QuaderKeyCode::Comma},
      KeyMapping{Qt::Key_Period, QuaderKeyCode::Period},
      KeyMapping{Qt::Key_Slash, QuaderKeyCode::Slash},
      KeyMapping{Qt::Key_Backslash, QuaderKeyCode::Backslash},
      KeyMapping{Qt::Key_Semicolon, QuaderKeyCode::Semicolon},
      KeyMapping{Qt::Key_Apostrophe, QuaderKeyCode::Apostrophe},
      KeyMapping{Qt::Key_BracketLeft, QuaderKeyCode::LeftBracket},
      KeyMapping{Qt::Key_BracketRight, QuaderKeyCode::RightBracket},
      KeyMapping{Qt::Key_Minus, QuaderKeyCode::Minus},
      KeyMapping{Qt::Key_Equal, QuaderKeyCode::Equal},
      KeyMapping{Qt::Key_QuoteLeft, QuaderKeyCode::Grave},
      KeyMapping{Qt::Key_Home, QuaderKeyCode::Home},
      KeyMapping{Qt::Key_End, QuaderKeyCode::End},
      KeyMapping{Qt::Key_PageUp, QuaderKeyCode::PageUp},
      KeyMapping{Qt::Key_PageDown, QuaderKeyCode::PageDown},
      KeyMapping{Qt::Key_Left, QuaderKeyCode::Left},
      KeyMapping{Qt::Key_Right, QuaderKeyCode::Right},
      KeyMapping{Qt::Key_Up, QuaderKeyCode::Up},
      KeyMapping{Qt::Key_Down, QuaderKeyCode::Down},
      KeyMapping{Qt::Key_Escape, QuaderKeyCode::Escape},
      KeyMapping{Qt::Key_Delete, QuaderKeyCode::Delete},
      KeyMapping{Qt::Key_Backspace, QuaderKeyCode::Backspace},
      KeyMapping{Qt::Key_Return, QuaderKeyCode::Enter},
      KeyMapping{Qt::Key_Tab, QuaderKeyCode::Tab},
      KeyMapping{Qt::Key_Backtab, QuaderKeyCode::Tab},
      KeyMapping{Qt::Key_Space, QuaderKeyCode::Space},
      KeyMapping{Qt::Key_Insert, QuaderKeyCode::Insert},
  };

  for (const KeyMapping &mapping : kMappings) {
    SCOPED_TRACE(mapping.qt_key);
    const std::optional<QuaderKeyCode> key = quader_key_code_from_qt_key(
        mapping.qt_key, Qt::NoModifier);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, mapping.quader_key);
  }
}

TEST(QtInputAdapterTests, NormalizesShiftedDigitsAndPunctuationToBaseKeys) {
  constexpr std::array kMappings{
      KeyMapping{Qt::Key_ParenRight, QuaderKeyCode::Digit0},
      KeyMapping{Qt::Key_Exclam, QuaderKeyCode::Digit1},
      KeyMapping{Qt::Key_At, QuaderKeyCode::Digit2},
      KeyMapping{Qt::Key_NumberSign, QuaderKeyCode::Digit3},
      KeyMapping{Qt::Key_Dollar, QuaderKeyCode::Digit4},
      KeyMapping{Qt::Key_Percent, QuaderKeyCode::Digit5},
      KeyMapping{Qt::Key_AsciiCircum, QuaderKeyCode::Digit6},
      KeyMapping{Qt::Key_Ampersand, QuaderKeyCode::Digit7},
      KeyMapping{Qt::Key_Asterisk, QuaderKeyCode::Digit8},
      KeyMapping{Qt::Key_ParenLeft, QuaderKeyCode::Digit9},
      KeyMapping{Qt::Key_Less, QuaderKeyCode::Comma},
      KeyMapping{Qt::Key_Greater, QuaderKeyCode::Period},
      KeyMapping{Qt::Key_Question, QuaderKeyCode::Slash},
      KeyMapping{Qt::Key_Bar, QuaderKeyCode::Backslash},
      KeyMapping{Qt::Key_Colon, QuaderKeyCode::Semicolon},
      KeyMapping{Qt::Key_QuoteDbl, QuaderKeyCode::Apostrophe},
      KeyMapping{Qt::Key_BraceLeft, QuaderKeyCode::LeftBracket},
      KeyMapping{Qt::Key_BraceRight, QuaderKeyCode::RightBracket},
      KeyMapping{Qt::Key_Underscore, QuaderKeyCode::Minus},
      KeyMapping{Qt::Key_Plus, QuaderKeyCode::Equal},
      KeyMapping{Qt::Key_AsciiTilde, QuaderKeyCode::Grave},
  };

  for (const KeyMapping &mapping : kMappings) {
    SCOPED_TRACE(mapping.qt_key);
    const QuaderInputChord chord = quader_input_chord_from_qt_key(
        mapping.qt_key, Qt::ShiftModifier);
    EXPECT_EQ(chord.key, mapping.quader_key);
    EXPECT_TRUE(chord.shift);
  }
}

TEST(QtInputAdapterTests, MapsEveryFunctionKeyThroughF24) {
  for (int offset = 0; offset < 24; ++offset) {
    SCOPED_TRACE(offset);
    const std::optional<QuaderKeyCode> key = quader_key_code_from_qt_key(
        Qt::Key_F1 + offset, Qt::NoModifier);
    ASSERT_TRUE(key.has_value());
    EXPECT_EQ(*key, static_cast<QuaderKeyCode>(
                        static_cast<int>(QuaderKeyCode::F1) + offset));
  }
}

TEST(QtInputAdapterTests, RejectsUnknownAndUnsupportedKeys) {
  EXPECT_FALSE(quader_key_code_from_qt_key(Qt::Key_unknown, Qt::NoModifier)
                   .has_value());
  EXPECT_FALSE(quader_key_code_from_qt_key(Qt::Key_Plus, Qt::NoModifier)
                   .has_value());
  EXPECT_TRUE(
      quader_input_chord_from_qt_key(Qt::Key_unknown, Qt::NoModifier).empty());
}

TEST(QtInputAdapterTests,
     KeyboardEventsUseEventTimeModifiersAndIgnoreAutoRepeat) {
  const Qt::KeyboardModifiers modifiers =
      Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;
  const QKeyEvent press(QEvent::KeyPress, Qt::Key_C, modifiers, "c", false,
                        1);
  const std::optional<QuaderInputChord> chord =
      quader_pressed_input_chord_from_qt_event(press);
  ASSERT_TRUE(chord.has_value());
  EXPECT_EQ(chord->device, QuaderInputDevice::Keyboard);
  EXPECT_EQ(chord->key, QuaderKeyCode::C);
  EXPECT_TRUE(chord->primary);
  EXPECT_TRUE(chord->shift);
  EXPECT_TRUE(chord->alt);

  const QKeyEvent meta_press(QEvent::KeyPress, Qt::Key_V,
                             Qt::MetaModifier, "v", false, 1);
  const std::optional<QuaderInputChord> meta_chord =
      quader_pressed_input_chord_from_qt_event(meta_press);
  ASSERT_TRUE(meta_chord.has_value());
  EXPECT_TRUE(meta_chord->primary);

  const QKeyEvent repeat(QEvent::KeyPress, Qt::Key_C, modifiers, "c", true,
                         2);
  EXPECT_FALSE(
      quader_pressed_input_chord_from_qt_event(repeat).has_value());

  const QKeyEvent release(QEvent::KeyRelease, Qt::Key_C, modifiers, "c",
                          false, 1);
  EXPECT_FALSE(
      quader_pressed_input_chord_from_qt_event(release).has_value());
}

TEST(QtInputAdapterTests, MapsEveryCanonicalMouseButton) {
  constexpr std::array kMappings{
      std::pair{Qt::LeftButton, QuaderMouseButton::Left},
      std::pair{Qt::MiddleButton, QuaderMouseButton::Middle},
      std::pair{Qt::RightButton, QuaderMouseButton::Right},
      std::pair{Qt::BackButton, QuaderMouseButton::Back},
      std::pair{Qt::ForwardButton, QuaderMouseButton::Forward},
      std::pair{Qt::ExtraButton3, QuaderMouseButton::Button6},
      std::pair{Qt::ExtraButton4, QuaderMouseButton::Button7},
      std::pair{Qt::ExtraButton5, QuaderMouseButton::Button8},
  };

  for (const auto &[qt_button, quader_button] : kMappings) {
    SCOPED_TRACE(static_cast<int>(qt_button));
    const std::optional<QuaderMouseButton> button =
        quader_mouse_button_from_qt_button(qt_button);
    ASSERT_TRUE(button.has_value());
    EXPECT_EQ(*button, quader_button);
  }
  EXPECT_FALSE(
      quader_mouse_button_from_qt_button(Qt::NoButton).has_value());
}

TEST(QtInputAdapterTests,
     MouseEventsMapPressDoubleClickAndDragWithEventTimeModifiers) {
  const Qt::KeyboardModifiers press_modifiers =
      Qt::MetaModifier | Qt::AltModifier;
  const QMouseEvent press =
      mouse_event(QEvent::MouseButtonPress, Qt::MiddleButton, press_modifiers);

  const std::optional<QuaderInputChord> press_chord =
      quader_mouse_input_chord_from_qt_event(press,
                                             QuaderInputGesture::Press);
  ASSERT_TRUE(press_chord.has_value());
  expect_mouse_chord(*press_chord, QuaderMouseButton::Middle,
                     QuaderInputGesture::Press);
  EXPECT_TRUE(press_chord->primary);
  EXPECT_FALSE(press_chord->shift);
  EXPECT_TRUE(press_chord->alt);

  const std::optional<QuaderInputChord> drag_chord =
      quader_mouse_input_chord_from_qt_event(press,
                                             QuaderInputGesture::Drag);
  ASSERT_TRUE(drag_chord.has_value());
  expect_mouse_chord(*drag_chord, QuaderMouseButton::Middle,
                     QuaderInputGesture::Drag);
  EXPECT_EQ(drag_chord->primary, press_chord->primary);
  EXPECT_EQ(drag_chord->shift, press_chord->shift);
  EXPECT_EQ(drag_chord->alt, press_chord->alt);

  const QMouseEvent double_click = mouse_event(
      QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::ShiftModifier);
  const std::optional<QuaderInputChord> double_click_chord =
      quader_mouse_input_chord_from_qt_event(
          double_click, QuaderInputGesture::DoubleClick);
  ASSERT_TRUE(double_click_chord.has_value());
  expect_mouse_chord(*double_click_chord, QuaderMouseButton::Left,
                     QuaderInputGesture::DoubleClick);
  EXPECT_TRUE(double_click_chord->shift);
}

TEST(QtInputAdapterTests, RejectsMismatchedMouseEventTypesAndWheelGestures) {
  const QMouseEvent press =
      mouse_event(QEvent::MouseButtonPress, Qt::RightButton);
  const QMouseEvent release =
      mouse_event(QEvent::MouseButtonRelease, Qt::RightButton);
  const QMouseEvent double_click =
      mouse_event(QEvent::MouseButtonDblClick, Qt::RightButton);

  EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(
                   press, QuaderInputGesture::DoubleClick)
                   .has_value());
  EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(
                   double_click, QuaderInputGesture::Press)
                   .has_value());
  EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(
                   release, QuaderInputGesture::Drag)
                   .has_value());
  EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(
                   press, QuaderInputGesture::Wheel)
                   .has_value());
}

TEST(QtInputAdapterTests, MapsWheelDirectionsAndSmoothPixelFallback) {
  constexpr std::array kDirections{
      std::pair{QPoint{0, 120}, QuaderInputGesture::WheelUp},
      std::pair{QPoint{0, -120}, QuaderInputGesture::WheelDown},
      std::pair{QPoint{120, 0}, QuaderInputGesture::WheelRight},
      std::pair{QPoint{-120, 0}, QuaderInputGesture::WheelLeft},
      std::pair{QPoint{30, 120}, QuaderInputGesture::WheelUp},
      std::pair{QPoint{-120, 30}, QuaderInputGesture::WheelLeft},
  };

  for (const auto &[delta, expected_gesture] : kDirections) {
    SCOPED_TRACE(delta.x());
    SCOPED_TRACE(delta.y());
    const QWheelEvent event = wheel_event({}, delta, Qt::ControlModifier);
    const std::optional<QuaderInputChord> chord =
        quader_wheel_input_chord_from_qt_event(event);
    ASSERT_TRUE(chord.has_value());
    expect_mouse_chord(*chord, QuaderMouseButton::None, expected_gesture);
    EXPECT_TRUE(chord->primary);
  }

  const QWheelEvent smooth =
      wheel_event(QPoint{0, -9}, {}, Qt::ShiftModifier | Qt::AltModifier);
  const std::optional<QuaderInputChord> smooth_chord =
      quader_wheel_input_chord_from_qt_event(smooth);
  ASSERT_TRUE(smooth_chord.has_value());
  expect_mouse_chord(*smooth_chord, QuaderMouseButton::None,
                     QuaderInputGesture::WheelDown);
  EXPECT_TRUE(smooth_chord->shift);
  EXPECT_TRUE(smooth_chord->alt);
}

TEST(QtInputAdapterTests, RejectsWheelEventsWithoutMotion) {
  const QWheelEvent event = wheel_event({}, {});
  EXPECT_FALSE(quader_wheel_input_chord_from_qt_event(event).has_value());
}

TEST(QtInputAdapterTests,
     HorizontalWheelKeepsDirectionalBindingWithoutGenericZoomInput) {
  constexpr std::array kHorizontalEvents{
      std::pair{QPoint{120, 0}, QuaderInputGesture::WheelRight},
      std::pair{QPoint{-120, 30}, QuaderInputGesture::WheelLeft},
  };

  for (const auto &[delta, expected_gesture] : kHorizontalEvents) {
    const QWheelEvent event = wheel_event({}, delta, Qt::ControlModifier);
    const quader::QtWheelInputTranslation translation =
        quader_wheel_input_from_qt_event(event);
    EXPECT_FLOAT_EQ(translation.mouse_wheel, 0.0f);
    ASSERT_EQ(translation.wheel_input_edges.size(), 1u);
    expect_mouse_chord(translation.wheel_input_edges.front().chord,
                       QuaderMouseButton::None, expected_gesture);
    EXPECT_TRUE(translation.wheel_input_edges.front().chord.primary);
    EXPECT_DOUBLE_EQ(translation.wheel_input_edges.front().step_delta,
                     delta.x() / 120.0);
  }
}

TEST(QtInputAdapterTests,
     VerticalWheelPreservesGenericBindingAndMouseWheelZoomInput) {
  const QWheelEvent event =
      wheel_event({}, QPoint{30, 120}, Qt::ShiftModifier);
  const quader::QtWheelInputTranslation translation =
      quader_wheel_input_from_qt_event(event);

  EXPECT_FLOAT_EQ(translation.mouse_wheel, 1.0f);
  ASSERT_EQ(translation.wheel_input_edges.size(), 2u);
  expect_mouse_chord(translation.wheel_input_edges[0].chord,
                     QuaderMouseButton::None, QuaderInputGesture::Wheel);
  expect_mouse_chord(translation.wheel_input_edges[1].chord,
                     QuaderMouseButton::None, QuaderInputGesture::WheelUp);
  EXPECT_TRUE(translation.wheel_input_edges[0].chord.shift);
  EXPECT_TRUE(translation.wheel_input_edges[1].chord.shift);
  EXPECT_DOUBLE_EQ(translation.wheel_input_edges[0].step_delta, 1.0);
  EXPECT_DOUBLE_EQ(translation.wheel_input_edges[1].step_delta, 1.0);

  const QWheelEvent smooth = wheel_event(QPoint{0, -9}, {});
  const quader::QtWheelInputTranslation smooth_translation =
      quader_wheel_input_from_qt_event(smooth);
  EXPECT_FLOAT_EQ(smooth_translation.mouse_wheel, -0.075f);
  ASSERT_EQ(smooth_translation.wheel_input_edges.size(), 2u);
  EXPECT_EQ(smooth_translation.wheel_input_edges[1].chord.gesture,
            QuaderInputGesture::WheelDown);
  EXPECT_DOUBLE_EQ(smooth_translation.wheel_input_edges[0].step_delta,
                   -0.075);
  EXPECT_DOUBLE_EQ(smooth_translation.wheel_input_edges[1].step_delta,
                   -0.075);

  const QWheelEvent two_notches = wheel_event({}, QPoint{0, 240});
  const quader::QtWheelInputTranslation two_notch_translation =
      quader_wheel_input_from_qt_event(two_notches);
  EXPECT_FLOAT_EQ(two_notch_translation.mouse_wheel, 2.0f);
  ASSERT_EQ(two_notch_translation.wheel_input_edges.size(), 2u);
  EXPECT_DOUBLE_EQ(two_notch_translation.wheel_input_edges[0].step_delta,
                   2.0);
  EXPECT_DOUBLE_EQ(two_notch_translation.wheel_input_edges[1].step_delta,
                   2.0);
}
