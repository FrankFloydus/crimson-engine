////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "editor/input/quader_input_binding.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QWheelEvent>

#include <algorithm>
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
using quader::quader_input_format_chord;
using quader::quader_input_parse_chord;
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
     MouseEventsMapPressClickDoubleClickAndDragWithEventTimeModifiers) {
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

  const QMouseEvent release = mouse_event(
      QEvent::MouseButtonRelease, Qt::MiddleButton, press_modifiers);
  const std::optional<QuaderInputChord> click_chord =
      quader_mouse_input_chord_from_qt_event(release,
                                             QuaderInputGesture::Click);
  ASSERT_TRUE(click_chord.has_value());
  expect_mouse_chord(*click_chord, QuaderMouseButton::Middle,
                     QuaderInputGesture::Click);
  EXPECT_TRUE(click_chord->primary);
  EXPECT_TRUE(click_chord->alt);

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
                   press, QuaderInputGesture::Click)
                   .has_value());
  EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(
                   double_click, QuaderInputGesture::Click)
                   .has_value());
  EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(
                   press, QuaderInputGesture::Wheel)
                   .has_value());
}

TEST(QtInputAdapterTests, CanonicalMouseClickParsesAndFormatsRoundTrip) {
  const std::optional<QuaderInputChord> chord =
      quader_input_parse_chord("Alt+MiddleMouseClick");
  ASSERT_TRUE(chord.has_value());
  expect_mouse_chord(*chord, QuaderMouseButton::Middle,
                     QuaderInputGesture::Click);
  EXPECT_TRUE(chord->alt);
  EXPECT_EQ(quader_input_format_chord(*chord), "Alt+MiddleMouseClick");
}

TEST(QtInputAdapterTests,
     DistinguishesBackslashSlashAndKeypadDivideBindings) {
  const std::optional<QuaderInputChord> backslash_symbol =
      quader_input_parse_chord("\\");
  const std::optional<QuaderInputChord> backslash_named =
      quader_input_parse_chord("Backslash");
  const std::optional<QuaderInputChord> slash =
      quader_input_parse_chord("/");
  const std::optional<QuaderInputChord> keypad_divide =
      quader_input_parse_chord("KeypadDivide");

  ASSERT_TRUE(backslash_symbol.has_value());
  ASSERT_TRUE(backslash_named.has_value());
  ASSERT_TRUE(slash.has_value());
  ASSERT_TRUE(keypad_divide.has_value());
  EXPECT_EQ(backslash_symbol->key, QuaderKeyCode::Backslash);
  EXPECT_EQ(backslash_named->key, QuaderKeyCode::Backslash);
  EXPECT_EQ(slash->key, QuaderKeyCode::Slash);
  EXPECT_EQ(keypad_divide->key, QuaderKeyCode::NumpadDivide);
  EXPECT_EQ(quader_input_format_chord(*backslash_symbol), "Backslash");
  EXPECT_EQ(quader_input_format_chord(*slash), "Slash");
  EXPECT_EQ(quader_input_format_chord(*keypad_divide), "KeypadDivide");
  EXPECT_NE(backslash_symbol->key, slash->key);
  EXPECT_NE(backslash_symbol->key, keypad_divide->key);
}

TEST(QtViewportSurfaceInputTests,
     RepeatedInitializationDoesNotSucceedWithoutASwapchain) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-initialization-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::RenderSystem render_system;
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);

  ASSERT_FALSE(render_system.initialized());
  EXPECT_FALSE(surface.initialize(render_system, 0x01u));
  EXPECT_FALSE(surface.ready_to_render());
  EXPECT_FALSE(surface.initialize(render_system, 0x01u));
  EXPECT_FALSE(surface.ready_to_render());
  EXPECT_FALSE(surface.render_frame());
}

TEST(QtViewportSurfaceInputTests,
     StationaryReleaseEmitsClickButPointerDragDoesNot) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-click-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  const QPointF press_local{24.0, 32.0};
  const QPointF press_global{124.0, 232.0};
  const Qt::KeyboardModifiers modifiers = Qt::AltModifier;

  QMouseEvent stationary_press(
      QEvent::MouseButtonPress, press_local, press_global,
      Qt::MiddleButton, Qt::MiddleButton, modifiers);
  QApplication::sendEvent(&surface, &stationary_press);
  const quader::QtViewportInputFrame press_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_TRUE(std::any_of(
      press_frame.input.pressed_input_chords.begin(),
      press_frame.input.pressed_input_chords.end(),
      [](const QuaderInputChord &chord) {
        return chord.mouse_button == QuaderMouseButton::Middle &&
               chord.gesture == QuaderInputGesture::Drag;
      }));

  QMouseEvent stationary_release(
      QEvent::MouseButtonRelease, press_local, press_global,
      Qt::MiddleButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &stationary_release);
  const quader::QtViewportInputFrame click_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_TRUE(std::any_of(
      click_frame.input.pressed_input_chords.begin(),
      click_frame.input.pressed_input_chords.end(),
      [](const QuaderInputChord &chord) {
        return chord.alt &&
               chord.mouse_button == QuaderMouseButton::Middle &&
               chord.gesture == QuaderInputGesture::Click;
      }));

  QMouseEvent drag_press(QEvent::MouseButtonPress, press_local, press_global,
                         Qt::MiddleButton, Qt::MiddleButton, modifiers);
  QApplication::sendEvent(&surface, &drag_press);
  (void)surface.take_input_frame(0.0f, false);

  const qreal drag_distance =
      static_cast<qreal>(QApplication::startDragDistance() + 2);
  const QPointF drag_local = press_local + QPointF{drag_distance, 0.0};
  const QPointF drag_global = press_global + QPointF{drag_distance, 0.0};
  QMouseEvent drag_move(QEvent::MouseMove, drag_local, drag_global,
                        Qt::NoButton, Qt::MiddleButton, modifiers);
  QApplication::sendEvent(&surface, &drag_move);
  (void)surface.take_input_frame(0.0f, false);

  QMouseEvent drag_release(QEvent::MouseButtonRelease, drag_local, drag_global,
                           Qt::MiddleButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &drag_release);
  const quader::QtViewportInputFrame drag_release_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_FALSE(std::any_of(
      drag_release_frame.input.pressed_input_chords.begin(),
      drag_release_frame.input.pressed_input_chords.end(),
      [](const QuaderInputChord &chord) {
        return chord.mouse_button == QuaderMouseButton::Middle &&
               chord.gesture == QuaderInputGesture::Click;
      }));
}

TEST(QtViewportSurfaceInputTests,
     CtrlRightClickReleaseAndDoubleClickStayDistinctFromDrag) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-material-click-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  const QPointF press_local{24.0, 32.0};
  const QPointF press_global{124.0, 232.0};
  const Qt::KeyboardModifiers modifiers = Qt::ControlModifier;
  const auto has_right_gesture = [](const quader::QtViewportInputFrame &frame,
                                    QuaderInputGesture gesture) {
    return std::any_of(
        frame.input.pressed_input_chords.begin(),
        frame.input.pressed_input_chords.end(),
        [&](const QuaderInputChord &chord) {
          return chord.primary && !chord.shift && !chord.alt &&
                 chord.mouse_button == QuaderMouseButton::Right &&
                 chord.gesture == gesture;
        });
  };

  QMouseEvent stationary_press(
      QEvent::MouseButtonPress, press_local, press_global,
      Qt::RightButton, Qt::RightButton, modifiers);
  QApplication::sendEvent(&surface, &stationary_press);
  const quader::QtViewportInputFrame press_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_TRUE(has_right_gesture(press_frame, QuaderInputGesture::Drag));
  EXPECT_FALSE(has_right_gesture(press_frame, QuaderInputGesture::Click));

  QMouseEvent stationary_release(
      QEvent::MouseButtonRelease, press_local, press_global,
      Qt::RightButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &stationary_release);
  const quader::QtViewportInputFrame click_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_TRUE(has_right_gesture(click_frame, QuaderInputGesture::Click));
  EXPECT_FALSE(
      has_right_gesture(click_frame, QuaderInputGesture::DoubleClick));

  QMouseEvent double_click(
      QEvent::MouseButtonDblClick, press_local, press_global,
      Qt::RightButton, Qt::RightButton, modifiers);
  QApplication::sendEvent(&surface, &double_click);
  const quader::QtViewportInputFrame double_click_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_TRUE(has_right_gesture(double_click_frame,
                                QuaderInputGesture::DoubleClick));
  EXPECT_FALSE(
      has_right_gesture(double_click_frame, QuaderInputGesture::Click));

  QMouseEvent double_release(
      QEvent::MouseButtonRelease, press_local, press_global,
      Qt::RightButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &double_release);
  const quader::QtViewportInputFrame double_release_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_FALSE(
      has_right_gesture(double_release_frame, QuaderInputGesture::Click));

  QMouseEvent drag_press(QEvent::MouseButtonPress, press_local, press_global,
                         Qt::RightButton, Qt::RightButton, modifiers);
  QApplication::sendEvent(&surface, &drag_press);
  (void)surface.take_input_frame(0.0f, false);

  const qreal drag_distance =
      static_cast<qreal>(QApplication::startDragDistance() + 2);
  const QPointF drag_local = press_local + QPointF{drag_distance, 0.0};
  const QPointF drag_global = press_global + QPointF{drag_distance, 0.0};
  QMouseEvent drag_move(QEvent::MouseMove, drag_local, drag_global,
                        Qt::NoButton, Qt::RightButton, modifiers);
  QApplication::sendEvent(&surface, &drag_move);
  (void)surface.take_input_frame(0.0f, false);

  QMouseEvent drag_release(QEvent::MouseButtonRelease, drag_local, drag_global,
                           Qt::RightButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &drag_release);
  const quader::QtViewportInputFrame drag_release_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_FALSE(has_right_gesture(drag_release_frame,
                                 QuaderInputGesture::Click));
}

TEST(QtViewportSurfaceInputTests,
     PhysicalGridGesturesReachTheWorkspaceThroughTheProductionPanelPath) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-grid-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());

  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  quader::QtViewportSurface *surface =
      panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);

  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        &session.pivot(), &session.linear_array(), &session.radial_array(),
        &session.point_placement(), 1.0 / 60.0);
  };
  const QPointF local{24.0, 32.0};
  const QPointF global{124.0, 232.0};
  const int default_preset = session.viewport_workspace().grid_preset();

  // Qt's Windows QPA converts Alt+vertical-wheel into a horizontal angle
  // delta. Exercise the exact native representation seen by Quader rather
  // than the idealized vertical delta used by adapter-only tests.
  QWheelEvent wheel_up(local, global, {}, QPoint{120, 0}, Qt::NoButton,
                       Qt::AltModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(surface, &wheel_up);
  process_input();
  EXPECT_EQ(session.viewport_workspace().grid_preset(), default_preset + 1);

  QWheelEvent wheel_down(local, global, {}, QPoint{-120, 0}, Qt::NoButton,
                         Qt::AltModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(surface, &wheel_down);
  process_input();
  EXPECT_EQ(session.viewport_workspace().grid_preset(), default_preset);

  ASSERT_TRUE(session.viewport_workspace().set_grid_preset(2));
  QMouseEvent middle_press(QEvent::MouseButtonPress, local, global,
                           Qt::MiddleButton, Qt::MiddleButton,
                           Qt::AltModifier);
  QApplication::sendEvent(surface, &middle_press);
  process_input();

  QMouseEvent middle_release(QEvent::MouseButtonRelease, local, global,
                             Qt::MiddleButton, Qt::NoButton,
                             Qt::AltModifier);
  QApplication::sendEvent(surface, &middle_release);
  process_input();
  EXPECT_EQ(session.viewport_workspace().grid_preset(), default_preset);
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

TEST(QtInputAdapterTests,
     RestoresWindowsAltVerticalWheelFromQtHorizontalRepresentation) {
  const QWheelEvent up =
      wheel_event({}, QPoint{120, 0}, Qt::AltModifier);
  const quader::QtWheelInputTranslation up_translation =
      quader_wheel_input_from_qt_event(up);
  ASSERT_EQ(up_translation.wheel_input_edges.size(), 2u);
  EXPECT_EQ(up_translation.wheel_input_edges[0].chord.gesture,
            QuaderInputGesture::Wheel);
  EXPECT_EQ(up_translation.wheel_input_edges[1].chord.gesture,
            QuaderInputGesture::WheelUp);
  EXPECT_TRUE(up_translation.wheel_input_edges[1].chord.alt);
  EXPECT_FLOAT_EQ(up_translation.mouse_wheel, 1.0f);
  EXPECT_DOUBLE_EQ(up_translation.wheel_input_edges[1].step_delta, 1.0);

  const QWheelEvent down =
      wheel_event({}, QPoint{-120, 0}, Qt::AltModifier);
  const quader::QtWheelInputTranslation down_translation =
      quader_wheel_input_from_qt_event(down);
  ASSERT_EQ(down_translation.wheel_input_edges.size(), 2u);
  EXPECT_EQ(down_translation.wheel_input_edges[0].chord.gesture,
            QuaderInputGesture::Wheel);
  EXPECT_EQ(down_translation.wheel_input_edges[1].chord.gesture,
            QuaderInputGesture::WheelDown);
  EXPECT_TRUE(down_translation.wheel_input_edges[1].chord.alt);
  EXPECT_FLOAT_EQ(down_translation.mouse_wheel, -1.0f);
  EXPECT_DOUBLE_EQ(down_translation.wheel_input_edges[1].step_delta, -1.0);
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
