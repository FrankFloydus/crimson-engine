////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "editor/input/quader_input_binding.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/interactions/viewport_navigation.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/localization_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/tools/primitive_creation_interaction.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "math/glm_bridge.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_pointer_gesture.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QPushButton>
#include <QTemporaryDir>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using quader::QuaderInputChord;
using quader::QuaderInputDevice;
using quader::QuaderInputGesture;
using quader::QuaderKeyCode;
using quader::QuaderMouseButton;
using quader::QtWheelDeltaSource;
using quader::QtWheelInputAccumulator;
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

void expect_history_unchanged(const quader::QuaderHistorySnapshot &actual,
                              const quader::QuaderHistorySnapshot &before) {
  EXPECT_EQ(actual.can_undo, before.can_undo);
  EXPECT_EQ(actual.can_redo, before.can_redo);
  EXPECT_EQ(actual.undo_label, before.undo_label);
  EXPECT_EQ(actual.redo_label, before.redo_label);
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

TEST(QtViewportSurfaceCursorPolicyTests,
     FocusAndCapturePrecedenceAreToolkitNeutral) {
  using Cursor = quader::ViewportPointerCursor;
  using Presentation = quader::QtViewportCursorPresentation;

  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, true, true, false),
            Presentation::PrecisionCross);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, false, true, false),
            Presentation::Default);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, true, false, false),
            Presentation::Default);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, true, true, true),
            Presentation::Hidden);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::Default, true, true, true),
            Presentation::Hidden);
  for (const auto cursor : {Cursor::OpenHand, Cursor::ClosedHand}) {
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, true, true, false),
              cursor == Cursor::OpenHand ? Presentation::OpenHand
                                         : Presentation::ClosedHand);
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, false, true, false),
              Presentation::Default);
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, true, false, false),
              Presentation::Default);
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, false, false, true),
              Presentation::Hidden);
  }
}

TEST(QtViewportSurfaceInputTests,
     HandCursorUsesNativeShapesCachesSettersAndRestoresOnAbandonment) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-hand-cursor-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QEnterEvent enter(QPointF{40, 40}, QPointF{40, 40}, QPointF{40, 40});
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(&surface, &enter);
  QApplication::sendEvent(&surface, &focus);
  for (const auto cursor : {quader::ViewportPointerCursor::OpenHand,
                             quader::ViewportPointerCursor::ClosedHand}) {
    surface.set_interaction_cursor(cursor);
    EXPECT_EQ(surface.cursor().shape(),
              cursor == quader::ViewportPointerCursor::OpenHand
                  ? Qt::OpenHandCursor : Qt::ClosedHandCursor);
    const auto revision = surface.pointer_cursor_presentation_revision();
    surface.set_interaction_cursor(cursor);
    EXPECT_EQ(surface.pointer_cursor_presentation_revision(), revision);
    QFocusEvent focus_out(QEvent::FocusOut);
    QApplication::sendEvent(&surface, &focus_out);
    EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
    QApplication::sendEvent(&surface, &focus);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(&surface, &leave);
    EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
    QApplication::sendEvent(&surface, &enter);
  }
  surface.abandon_input();
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
}

TEST(QtViewportSurfaceInputTests,
     PrecisionCursorCachesUpdatesAndClearsOnFocusOrHoverLoss) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-cursor-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  surface.resize(800, 600);
  surface.set_interaction_cursor(
      quader::ViewportPointerCursor::PrecisionCross);
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
  EXPECT_EQ(surface.pointer_cursor_presentation_revision(), 0u);

  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(&surface, &enter);
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
  QFocusEvent focus_in(QEvent::FocusIn);
  QApplication::sendEvent(&surface, &focus_in);
  EXPECT_EQ(surface.cursor().shape(), Qt::CrossCursor);
  const std::uint64_t stable_revision =
      surface.pointer_cursor_presentation_revision();

  surface.set_interaction_cursor(
      quader::ViewportPointerCursor::PrecisionCross);
  surface.set_interaction_cursor(
      quader::ViewportPointerCursor::PrecisionCross);
  EXPECT_EQ(surface.pointer_cursor_presentation_revision(), stable_revision);

  QFocusEvent focus_out(QEvent::FocusOut);
  QApplication::sendEvent(&surface, &focus_out);
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
  QApplication::sendEvent(&surface, &focus_in);
  EXPECT_EQ(surface.cursor().shape(), Qt::CrossCursor);

  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&surface, &leave);
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
  QApplication::sendEvent(&surface, &enter);
  EXPECT_EQ(surface.cursor().shape(), Qt::CrossCursor);

  surface.set_interaction_cursor(quader::ViewportPointerCursor::Default);
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
}

TEST(QtViewportSurfaceInputTests,
     ProductionPanelRoutesPrimitiveCursorAndNavigationWithoutToolBranches) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-primitive-cursor-panel-tests";
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
  surface->resize(800, 600);

  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);
  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };

  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);
  auto *inactive_surface = panel.surface(quader::ViewportPaneId::Top);
  ASSERT_NE(inactive_surface, nullptr);
  QEnterEvent inactive_enter(local, local, global);
  QApplication::sendEvent(inactive_surface, &inactive_enter);
  QApplication::sendEvent(inactive_surface, &focus);
  EXPECT_EQ(inactive_surface->cursor().shape(), Qt::ArrowCursor);
  QEvent inactive_leave(QEvent::Leave);
  QFocusEvent inactive_focus_out(QEvent::FocusOut);
  QApplication::sendEvent(inactive_surface, &inactive_leave);
  QApplication::sendEvent(inactive_surface, &inactive_focus_out);
  const std::uint64_t stable_revision =
      surface->pointer_cursor_presentation_revision();
  process_input();
  EXPECT_EQ(surface->pointer_cursor_presentation_revision(), stable_revision);

  QMouseEvent orbit_press(QEvent::MouseButtonPress, local, global,
                          Qt::MiddleButton, Qt::MiddleButton,
                          Qt::NoModifier);
  QApplication::sendEvent(surface, &orbit_press);
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);

  QMouseEvent orbit_release(QEvent::MouseButtonRelease, local, global,
                            Qt::MiddleButton, Qt::NoButton,
                            Qt::NoModifier);
  QApplication::sendEvent(surface, &orbit_release);
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
}

TEST(QtViewportSurfaceInputTests,
     ModifierOnlyKeysUpdateStationaryViewportInput) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-stationary-modifier-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(&surface, &focus);

  // The modifier key itself determines its transition even when the event's
  // modifier mask describes the state immediately before that transition.
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
  QApplication::sendEvent(&surface, &press);
  const auto down = surface.take_input_frame(1.0f / 60.0f, false);
  EXPECT_TRUE(down.input.modifiers.primary);
  EXPECT_TRUE(down.input.pressed_input_chords.empty());
  EXPECT_TRUE(down.input.pointer_pressed_edges.empty());
  EXPECT_FLOAT_EQ(down.input.mouse_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(down.input.mouse_delta.y, 0.0f);

  quader::ViewportInteractionController controller;
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800, 600);
  camera.focus_origin(10);
  const quader::QuaderPrimitiveCreationState cube{
      .object_id = 1, .local_min = {-1, -1, -1}, .local_max = {1, 1, 1},
      .world_x = {1, 0, 0}, .world_y = {0, 1, 0}, .world_z = {0, 0, 1}};
  (void)controller.primitive_creation_controls(cube, camera, 1);
  controller.set_primitive_creation_decrement(down.input.modifiers.primary);
  const auto held_revision = controller.primitive_creation_controls(cube, camera, 1).revision;
  // Exercise the real Qt surface frame sampler on Windows too: no mouse
  // event, Enter/hover, or tool frame is needed to retain and project Ctrl.
  for (int frame = 0; frame < 60; ++frame) {
    const auto idle = surface.take_input_frame(1.0f / 60.0f, false);
    EXPECT_TRUE(idle.input.modifiers.primary);
    EXPECT_FALSE(idle.input.viewport_hovered);
    EXPECT_TRUE(idle.input.pressed_input_chords.empty());
    EXPECT_TRUE(idle.input.active_input_chords.empty());
    controller.set_primitive_creation_decrement(idle.input.modifiers.primary);
    const auto &controls = controller.primitive_creation_controls(cube, camera, 1);
    EXPECT_EQ(controls.revision, held_revision);
    EXPECT_EQ(controls.hovered, -1);
    for (int index : {6, 7, 8}) EXPECT_TRUE(controls.controls[index].subdivision_decrement);
  }

  QKeyEvent release(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
  QApplication::sendEvent(&surface, &release);
  const auto up = surface.take_input_frame(1.0f / 60.0f, false);
  EXPECT_FALSE(up.input.modifiers.primary);
  EXPECT_TRUE(up.input.pressed_input_chords.empty());
  EXPECT_TRUE(up.input.released_input_chords.empty());
  EXPECT_TRUE(up.input.pointer_released_edges.empty());
  EXPECT_FLOAT_EQ(up.input.mouse_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(up.input.mouse_delta.y, 0.0f);
  controller.set_primitive_creation_decrement(up.input.modifiers.primary);
  const auto &released = controller.primitive_creation_controls(cube, camera, 1);
  EXPECT_GT(released.revision, held_revision);
  for (int index : {6, 7, 8}) EXPECT_FALSE(released.controls[index].subdivision_decrement);
  EXPECT_FALSE(surface.take_input_frame(1.0f / 60.0f, false).input.modifiers.primary);
}

TEST(QtViewportSurfaceInputTests,
     CoalescedMouseEdgesPreservePressModifiersIndependentlyOfLiveState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-pointer-modifier-provenance-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  const QPointF point{40, 50};
  QKeyEvent ctrl_down(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
  QMouseEvent press(QEvent::MouseButtonPress, point, point,
                    Qt::LeftButton, Qt::LeftButton, Qt::ControlModifier);
  QKeyEvent ctrl_up(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
  QMouseEvent release(QEvent::MouseButtonRelease, point, point,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&surface, &ctrl_down);
  QApplication::sendEvent(&surface, &press);
  QApplication::sendEvent(&surface, &ctrl_up);
  QApplication::sendEvent(&surface, &release);
  const auto sample = surface.take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(sample.input.pointer_pressed_edges.size(), 1u);
  ASSERT_EQ(sample.input.pointer_released_edges.size(), 1u);
  ASSERT_TRUE(sample.input.pointer_pressed_edges[0].modifiers);
  ASSERT_TRUE(sample.input.pointer_released_edges[0].modifiers);
  EXPECT_TRUE(sample.input.pointer_pressed_edges[0].modifiers->primary);
  EXPECT_FALSE(sample.input.pointer_released_edges[0].modifiers->primary);
  EXPECT_FALSE(sample.input.modifiers.primary);
  const auto gesture = quader::viewport_pointer_gesture_from_latest_edge(
      sample.input.pointer_pressed_edges, QuaderMouseButton::Left,
      sample.left_click_local, sample.input.mouse_screen, sample.input.modifiers);
  EXPECT_TRUE(gesture.modifiers.primary);
  EXPECT_NE(quader::viewport_pointer_edge_after(
                sample.input.pointer_released_edges, gesture.press), nullptr);

  // Legacy synthetic edges without provenance keep the explicit fallback.
  const std::array<quader::ViewportPointerButtonEdge, 1> synthetic{{
      {.button = QuaderMouseButton::Left}}};
  EXPECT_TRUE(quader::viewport_pointer_gesture_from_latest_edge(
      synthetic, QuaderMouseButton::Left, {}, {}, {.primary = true}).modifiers.primary);
}

TEST(QtViewportSurfaceInputTests,
     FreehandSamplesPreserveClosedLoopsAndCoalescedGestureIdentity) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-freehand-coalesced-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  const std::array<QPointF, 5> loop{{{40, 50}, {90, 50}, {90, 100},
                                   {40, 100}, {40, 50}}};
  const auto send = [&](QEvent::Type type, QPointF local) {
    const bool move = type == QEvent::MouseMove;
    QMouseEvent event(type, local, local + QPointF{100, 200},
                      move ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                        : Qt::LeftButton,
                      Qt::NoModifier);
    QApplication::sendEvent(&surface, &event);
  };
  send(QEvent::MouseButtonPress, loop.front());
  for (std::size_t index = 1; index < loop.size() - 1; ++index) {
    send(QEvent::MouseMove, loop[index]);
  }
  send(QEvent::MouseButtonRelease, loop.back());
  send(QEvent::MouseButtonPress, {140, 150});
  send(QEvent::MouseButtonRelease, {140, 150});

  const auto frame = surface.take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(frame.input.pointer_pressed_edges.size(), 2u);
  ASSERT_EQ(frame.input.pointer_released_edges.size(), 2u);
  const auto &samples = frame.input.pointer_motion_samples;
  ASSERT_EQ(samples.size(), loop.size() + 2u);
  const auto first_press = frame.input.pointer_pressed_edges[0].sequence;
  const auto second_press = frame.input.pointer_pressed_edges[1].sequence;
  const float scale = frame.input.device_pixel_ratio;
  for (std::size_t index = 0; index < loop.size(); ++index) {
    SCOPED_TRACE(index);
    EXPECT_EQ(samples[index].button, QuaderMouseButton::Left);
    EXPECT_EQ(samples[index].press_sequence, first_press);
    EXPECT_FLOAT_EQ(samples[index].local.x,
                    static_cast<float>(loop[index].x()) * scale);
    EXPECT_FLOAT_EQ(samples[index].local.y,
                    static_cast<float>(loop[index].y()) * scale);
  }
  EXPECT_EQ(samples.front().sequence, first_press);
  EXPECT_EQ(samples[loop.size() - 1u].sequence,
            frame.input.pointer_released_edges[0].sequence);
  for (std::size_t index = 1; index < samples.size(); ++index) {
    EXPECT_GT(samples[index].sequence, samples[index - 1u].sequence);
  }
  EXPECT_EQ(samples[loop.size()].press_sequence, second_press);
  EXPECT_EQ(samples.back().press_sequence, second_press);
  EXPECT_EQ(samples.back().sequence,
            frame.input.pointer_released_edges[1].sequence);
  EXPECT_TRUE(surface.take_input_frame(1.0f / 60.0f, false)
                  .input.pointer_motion_samples.empty());
}

TEST(QtViewportSurfaceInputTests,
     FreehandSamplesSurviveFrameDrainsAndClosedMotionRemainsActivity) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-freehand-frame-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  const QPointF origin{40, 50};
  QMouseEvent press(QEvent::MouseButtonPress, origin, origin,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&surface, &press);
  const auto pressed = surface.take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(pressed.input.pointer_motion_samples.size(), 1u);
  const auto press_sequence = pressed.input.pointer_pressed_edges[0].sequence;
  for (const QPointF point : {QPointF{90, 50}, QPointF{90, 100}, origin}) {
    QMouseEvent move(QEvent::MouseMove, point, point, Qt::NoButton,
                     Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&surface, &move);
  }
  const auto moved = surface.take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(moved.input.pointer_motion_samples.size(), 3u);
  EXPECT_TRUE(moved.input.pointer_pressed_edges.empty());
  EXPECT_TRUE(moved.input.pointer_released_edges.empty());
  EXPECT_FLOAT_EQ(moved.input.mouse_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(moved.input.mouse_delta.y, 0.0f);
  EXPECT_TRUE(moved.pointer_activity());
  for (const auto &sample : moved.input.pointer_motion_samples) {
    EXPECT_EQ(sample.press_sequence, press_sequence);
    EXPECT_GT(sample.sequence, press_sequence);
  }

  QMouseEvent release(QEvent::MouseButtonRelease, origin, origin,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&surface, &release);
  const auto released = surface.take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(released.input.pointer_motion_samples.size(), 1u);
  EXPECT_EQ(released.input.pointer_motion_samples[0].press_sequence,
            press_sequence);
  EXPECT_EQ(released.input.pointer_motion_samples[0].sequence,
            released.input.pointer_released_edges[0].sequence);
}

TEST(QtViewportSurfaceInputTests,
     FreehandSamplesExcludeHoverAndNavigationAndClearOnFocusOrScaleChange) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-freehand-cancel-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  for (const auto button : {Qt::NoButton, Qt::MiddleButton, Qt::RightButton}) {
    if (button != Qt::NoButton) {
      auto press = mouse_event(QEvent::MouseButtonPress, button);
      QApplication::sendEvent(&surface, &press);
    }
    QMouseEvent move(QEvent::MouseMove, {40, 50}, {140, 250}, Qt::NoButton,
                     button, Qt::NoModifier);
    QApplication::sendEvent(&surface, &move);
    if (button != Qt::NoButton) {
      auto release = mouse_event(QEvent::MouseButtonRelease, button);
      QApplication::sendEvent(&surface, &release);
    }
    EXPECT_TRUE(surface.take_input_frame(1.0f / 60.0f, false)
                    .input.pointer_motion_samples.empty());
  }
  for (const auto interruption : {QEvent::FocusOut,
                                  QEvent::DevicePixelRatioChange}) {
    auto press = mouse_event(QEvent::MouseButtonPress, Qt::LeftButton);
    QApplication::sendEvent(&surface, &press);
    QEvent scale_changed(QEvent::DevicePixelRatioChange);
    QFocusEvent focus_lost(QEvent::FocusOut);
    QApplication::sendEvent(&surface, interruption == QEvent::FocusOut
                                         ? &focus_lost : &scale_changed);
    const auto canceled = surface.take_input_frame(1.0f / 60.0f, false);
    EXPECT_TRUE(canceled.input.pointer_interaction_canceled);
    EXPECT_TRUE(canceled.input.pointer_motion_samples.empty());
    auto release = mouse_event(QEvent::MouseButtonRelease, Qt::LeftButton);
    QApplication::sendEvent(&surface, &release);
    EXPECT_TRUE(surface.take_input_frame(1.0f / 60.0f, false)
                    .input.pointer_motion_samples.empty());
  }
}

TEST(QtViewportSurfaceInputTests,
     MouseCommandsReserveCubeControlsButKeyboardFlipAllowsResize) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-cube-command-pointer-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(std::filesystem::path(__FILE__).parent_path().parent_path() /
                                    "resources" / "locales", "en-US"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  quader::ModelingBoxPreview preview;
  preview.ok = preview.valid = true;
  preview.drag_state = quader::ModelingBoxDragState::Height;
  preview.corners = {quader::QVec3{-1, -1, -1}, quader::QVec3{-1, -1, 1},
                     quader::QVec3{1, -1, 1}, quader::QVec3{1, -1, -1},
                     quader::QVec3{-1, 1, -1}, quader::QVec3{-1, 1, 1},
                     quader::QVec3{1, 1, 1}, quader::QVec3{1, 1, -1}};
  preview.corner_count = 8;
  // One world unit is 32 map units; width also exercises fractional map units.
  for (auto &corner : preview.corners) {
    if (corner.x > 0) corner.x = 1.1328125f;
    if (corner.y > 0) corner.y = 0.0f;
  }
  ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Cube").ok);

  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  panel.set_localization(localization);
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);
  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  auto &camera = session.viewport_workspace().camera(
      quader::ViewportPaneId::Perspective);
  camera.focus_origin(10);
  process_input();
  const auto local_point = [&](quader::QVec2 pixels) {
    return QPointF{pixels.x / surface->devicePixelRatio(),
                   pixels.y / surface->devicePixelRatio()};
  };
  quader::PrimitiveCreationInteraction projection;
  const auto control_point = [&](int index) {
    const auto &frame = projection.project(
        quader::ViewportPrimitiveCreationApiAdapter::from_api(
            editor.tools().live_primitive_cube()), camera,
        panel.viewport_raster_scale(quader::ViewportPaneId::Perspective));
    return local_point(frame.controls[index].screen_position);
  };
  const auto send_mouse = [&](QEvent::Type type, QPointF local,
                              Qt::KeyboardModifiers modifiers) {
    QMouseEvent event(type, local, local + QPointF{100, 100},
                      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                      type == QEvent::MouseButtonRelease ? Qt::NoButton
                                                        : Qt::LeftButton,
                      modifiers);
    QApplication::sendEvent(surface, &event);
  };
  const auto send_ctrl = [&](bool down) {
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease,
                    Qt::Key_Control, down ? Qt::ControlModifier : Qt::NoModifier);
    QApplication::sendEvent(surface, &event);
  };
  constexpr auto kFlip = quader::quader_command::kToolsFlipPrimitiveFaces;
  // Keep the button above its decrement limit so an unreserved command click
  // would visibly change the cube and fail the ownership assertions below.
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kBoxSubdivisionsXSetting, 2.0).ok());
  for (const bool coalesced : {false, true}) {
    for (const int control : {0, 6}) {
      SCOPED_TRACE(coalesced);
      SCOPED_TRACE(control);
      ASSERT_TRUE(editor.commands().set_shortcut(kFlip, "Ctrl+LeftMouse").ok());
      const auto before = editor.tools().live_primitive_cube();
      ASSERT_TRUE(before);
      const QPointF point = control_point(control);
      send_mouse(QEvent::MouseButtonPress, point, Qt::ControlModifier);
      if (!coalesced) {
        process_input();
        ASSERT_TRUE(editor.tools().live_primitive_cube());
        EXPECT_FALSE(editor.tools().live_primitive_cube()->resizing);
        // Ownership survives rebinding and a modifier change while held.
        ASSERT_TRUE(editor.commands().reset_shortcut(kFlip).ok());
        send_mouse(QEvent::MouseMove, point + QPointF{40, 0}, Qt::NoModifier);
        process_input();
        send_mouse(QEvent::MouseMove, point, Qt::NoModifier);
        process_input();
      }
      send_mouse(QEvent::MouseButtonRelease, point, Qt::NoModifier);
      process_input();
      const auto after = editor.tools().live_primitive_cube();
      ASSERT_TRUE(after);
      EXPECT_NE(after->flip_faces, before->flip_faces);
      EXPECT_EQ(after->subdivisions, before->subdivisions);
      EXPECT_EQ(after->local_min.x, before->local_min.x);
      EXPECT_EQ(after->local_max.x, before->local_max.x);
      EXPECT_FALSE(after->resizing);
      EXPECT_EQ(session.modeling().box_drag_state(), quader::ModelingBoxDragState::Idle);
      EXPECT_EQ(session.primitive_creation().shape_count(), 1);
    }
  }

  // A mouse sequence prefix reserves its press even before a command executes.
  ASSERT_TRUE(editor.commands().set_shortcut(kFlip, "Ctrl+LeftMouse F").ok());
  const auto before_sequence = editor.tools().live_primitive_cube();
  const QPointF plus = control_point(6);
  send_mouse(QEvent::MouseButtonPress, plus, Qt::ControlModifier);
  send_mouse(QEvent::MouseButtonRelease, plus, Qt::NoModifier);
  process_input();
  EXPECT_EQ(editor.tools().live_primitive_cube()->subdivisions,
            before_sequence->subdivisions);
  EXPECT_EQ(editor.tools().live_primitive_cube()->flip_faces,
            before_sequence->flip_faces);
  QKeyEvent sequence_finish(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
  QKeyEvent sequence_release(QEvent::KeyRelease, Qt::Key_F, Qt::NoModifier);
  QApplication::sendEvent(surface, &sequence_finish);
  QApplication::sendEvent(surface, &sequence_release);
  process_input();
  EXPECT_NE(editor.tools().live_primitive_cube()->flip_faces,
            before_sequence->flip_faces);

  // A keyboard command in the same frame must leave an unrelated click usable.
  ASSERT_TRUE(editor.commands().reset_shortcut(kFlip).ok());
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kBoxSubdivisionsXSetting, 2.0).ok());
  const auto before_click = editor.tools().live_primitive_cube();
  ASSERT_TRUE(before_click);
  QKeyEvent flip_press(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
  QKeyEvent flip_release(QEvent::KeyRelease, Qt::Key_F, Qt::NoModifier);
  QApplication::sendEvent(surface, &flip_press);
  QApplication::sendEvent(surface, &flip_release);
  send_ctrl(true);
  send_mouse(QEvent::MouseButtonPress, plus, Qt::ControlModifier);
  send_ctrl(false);
  send_mouse(QEvent::MouseButtonRelease, plus, Qt::NoModifier);
  process_input();
  ASSERT_TRUE(editor.tools().live_primitive_cube());
  EXPECT_EQ(editor.tools().live_primitive_cube()->subdivisions[0],
            before_click->subdivisions[0] - 1);
  EXPECT_NE(editor.tools().live_primitive_cube()->flip_faces, before_click->flip_faces);

  constexpr std::array<std::string_view, 3> kSubdivisionSettings{
      quader::kBoxSubdivisionsXSetting, quader::kBoxSubdivisionsYSetting,
      quader::kBoxSubdivisionsZSetting};
  for (const bool coalesced : {false, true}) {
    for (int axis = 0; axis < 3; ++axis) {
      SCOPED_TRACE(coalesced);
      SCOPED_TRACE(axis);
      for (const auto setting : kSubdivisionSettings) {
        ASSERT_TRUE(editor.tools().set_setting(
            quader::kCreatePrimitiveToolId, setting, 2.0).ok());
      }
      const QPointF button = control_point(6 + axis);
      for (const int initial : {2, 1}) {
        SCOPED_TRACE(initial);
        const auto before = editor.tools().live_primitive_cube();
        ASSERT_TRUE(before);
        ASSERT_EQ(before->subdivisions[axis], initial);
        send_ctrl(true);
        send_mouse(QEvent::MouseButtonPress, button, Qt::ControlModifier);
        if (!coalesced) process_input();
        // Release Ctrl first, including a separate stationary frame while
        // the mouse is held. The press still owns the decrement operation.
        send_ctrl(false);
        if (!coalesced) process_input();
        send_mouse(QEvent::MouseButtonRelease, button, Qt::NoModifier);
        process_input();
        const auto after = editor.tools().live_primitive_cube();
        ASSERT_TRUE(after);
        auto expected = before->subdivisions;
        expected[axis] = 1;
        EXPECT_EQ(after->subdivisions, expected);
        if (initial == 1) EXPECT_EQ(after->revision, before->revision);
        EXPECT_FALSE(after->resizing);
        EXPECT_EQ(session.modeling().box_drag_state(), quader::ModelingBoxDragState::Idle);
        EXPECT_EQ(session.primitive_creation().shape_count(), 1);
      }

      // The inverse ordering also freezes intent: Ctrl pressed after the
      // mouse-down changes live mode, but the accepted click increments.
      send_mouse(QEvent::MouseButtonPress, button, Qt::NoModifier);
      if (!coalesced) process_input();
      send_ctrl(true);
      send_mouse(QEvent::MouseButtonRelease, button, Qt::ControlModifier);
      process_input();
      ASSERT_TRUE(editor.tools().live_primitive_cube());
      EXPECT_EQ(editor.tools().live_primitive_cube()->subdivisions[axis], 2);
      send_ctrl(false);
      process_input();
    }
  }

  auto *dimension_label = panel.findChild<QLabel *>(
      QStringLiteral("viewportPrimitiveFeedback_perspective"));
  ASSERT_NE(dimension_label, nullptr);
  for (const int index : {2, 3, 7}) {
    SCOPED_TRACE(index);
    const QPointF height_control = control_point(index);
    QMouseEvent hover_height(QEvent::MouseMove, height_control,
                             height_control + QPointF{100, 100},
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &hover_height);
    process_input();
    ASSERT_FALSE(dimension_label->isHidden());
    EXPECT_EQ(dimension_label->text(), QStringLiteral("Height: 32"));
  }
  const QPointF width_button = control_point(6);
  QMouseEvent hover_width(QEvent::MouseMove, width_button, width_button + QPointF{100, 100},
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &hover_width);
  process_input();
  ASSERT_FALSE(dimension_label->isHidden());
  EXPECT_EQ(dimension_label->text(), QStringLiteral("Width: 68.25"));
  const auto feedback_updates = panel.primitive_feedback_update_count();
  for (int frame = 0; frame < 20; ++frame) process_input();
  EXPECT_EQ(panel.primitive_feedback_update_count(), feedback_updates);

  // F during a held resize keeps its owner and allows subsequent movement.
  const auto before_resize = editor.tools().live_primitive_cube();
  ASSERT_TRUE(before_resize);
  const QPointF dot = control_point(0);
  QMouseEvent hover_handle(QEvent::MouseMove, dot, dot + QPointF{100, 100},
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &hover_handle);
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::OpenHandCursor);
  EXPECT_EQ(dimension_label->text(), QStringLiteral("Width: 68.25"));
  send_mouse(QEvent::MouseButtonPress, dot, Qt::NoModifier);
  process_input();
  ASSERT_TRUE(editor.tools().live_primitive_cube());
  ASSERT_TRUE(editor.tools().live_primitive_cube()->resizing);
  EXPECT_EQ(surface->cursor().shape(), Qt::OpenHandCursor);
  QKeyEvent drag_flip_press(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
  QKeyEvent drag_flip_release(QEvent::KeyRelease, Qt::Key_F, Qt::NoModifier);
  QApplication::sendEvent(surface, &drag_flip_press);
  QApplication::sendEvent(surface, &drag_flip_release);
  process_input();
  EXPECT_TRUE(editor.tools().live_primitive_cube()->resizing);
  EXPECT_NE(editor.tools().live_primitive_cube()->flip_faces, before_resize->flip_faces);
  const auto &frame = projection.frame();
  const QPointF moved = local_point(camera.project_world_point(
      frame.controls[0].world_position + before_resize->world_x * 2.0f).position);
  send_mouse(QEvent::MouseMove, moved, Qt::NoModifier);
  process_input();
  EXPECT_GT(editor.tools().live_primitive_cube()->local_max.x, before_resize->local_max.x);
  EXPECT_EQ(surface->cursor().shape(), Qt::ClosedHandCursor);
  EXPECT_EQ(dimension_label->text(), QStringLiteral("Width: 132.25"));
  ASSERT_TRUE(localization.select_locale("it-IT"));
  process_input();
  EXPECT_EQ(dimension_label->text(), QStringLiteral("Larghezza: 132,25"));
  send_mouse(QEvent::MouseButtonRelease, moved, Qt::NoModifier);
  process_input();
  EXPECT_FALSE(editor.tools().live_primitive_cube()->resizing);
  EXPECT_NE(surface->cursor().shape(), Qt::ClosedHandCursor);
  EXPECT_EQ(session.primitive_creation().shape_count(), 1);
}

TEST(QtViewportSurfaceInputTests, CylinderCapsFlipRadiusAndCtrlSegmentsShareNativeRouting) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-cylinder-viewport-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kPrimitiveTypeSetting, 1.0).ok());
  quader::ModelingBoxPreview preview;
  preview.ok = preview.valid = true;
  preview.drag_state = quader::ModelingBoxDragState::Height;
  preview.corners = {quader::QVec3{-1, -1, -1}, quader::QVec3{-1, -1, 1},
                     quader::QVec3{1, -1, 1}, quader::QVec3{1, -1, -1},
                     quader::QVec3{-1, 1, -1}, quader::QVec3{-1, 1, 1},
                     quader::QVec3{1, 1, 1}, quader::QVec3{1, 1, -1}};
  preview.corner_count = 8;
  ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Cylinder").ok);
  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);
  auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  camera.focus_origin(10);
  const auto process = [&] {
    panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  const auto mouse = [&](QEvent::Type type, quader::QVec2 pixels, Qt::KeyboardModifiers modifiers) {
    const QPointF local{pixels.x / surface->devicePixelRatio(), pixels.y / surface->devicePixelRatio()};
    QMouseEvent event(type, local, QPointF(surface->mapToGlobal(local.toPoint())),
                     type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                     type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
    QApplication::sendEvent(surface, &event);
  };
  const auto key = [&](int code) {
    QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
    QApplication::sendEvent(surface, &press);
    QApplication::sendEvent(surface, &release);
  };
  process();
  quader::PrimitiveCreationInteraction projection;
  const auto before = editor.tools().live_primitive_creation();
  ASSERT_TRUE(before);
  const auto control = projection.project(
      quader::ViewportPrimitiveCreationApiAdapter::from_api(before), camera,
      static_cast<float>(surface->devicePixelRatio())).controls[0];
  mouse(QEvent::MouseButtonPress, control.screen_position, Qt::NoModifier);
  process();
  ASSERT_TRUE(editor.tools().live_primitive_creation()->resizing);
  key(Qt::Key_C);
  key(Qt::Key_F);
  process();
  ASSERT_TRUE(editor.tools().live_primitive_creation());
  EXPECT_FALSE(editor.tools().live_primitive_creation()->caps);
  EXPECT_TRUE(editor.tools().live_primitive_creation()->flip_faces);
  EXPECT_TRUE(editor.tools().live_primitive_creation()->resizing);
  const auto end = camera.project_world_point(control.world_position +
      before->world_x * control.resize_axis.x + before->world_z * control.resize_axis.z).position;
  mouse(QEvent::MouseMove, end, Qt::NoModifier);
  process();
  mouse(QEvent::MouseButtonRelease, end, Qt::NoModifier);
  process();
  const auto resized = editor.tools().live_primitive_creation();
  ASSERT_TRUE(resized);
  EXPECT_FALSE(resized->resizing);
  EXPECT_GT(resized->local_max.x, before->local_max.x);
  const auto button = projection.project(
      quader::ViewportPrimitiveCreationApiAdapter::from_api(resized), camera,
      static_cast<float>(surface->devicePixelRatio())).controls[6];
  mouse(QEvent::MouseButtonPress, button.screen_position, Qt::ControlModifier);
  mouse(QEvent::MouseButtonRelease, button.screen_position, Qt::NoModifier);
  process();
  const auto after = editor.tools().live_primitive_creation();
  ASSERT_TRUE(after);
  EXPECT_EQ(after->side_count, resized->side_count - 1);
  EXPECT_FALSE(after->caps);
  EXPECT_TRUE(after->flip_faces);
  EXPECT_FLOAT_EQ(after->local_max.x, resized->local_max.x);
  EXPECT_EQ(session.primitive_creation().shape_count(), 1);
  EXPECT_EQ(session.modeling().box_drag_state(), quader::ModelingBoxDragState::Idle);
}

TEST(QtViewportSurfaceInputTests,
     RoundPrimitiveButtonsRouteSettingsAndRetainLocalizedFeedback) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-round-primitive-feedback-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(std::filesystem::path(__FILE__).parent_path().parent_path() /
                                    "resources" / "locales", "en-US"));
  const auto counts = [](const quader::QuaderPrimitiveCreationState &state) {
    return std::array<int, 5>{state.side_count, state.radial_segments, state.rings,
                              state.major_segments, state.minor_segments};
  };
  for (const auto kind : {quader::PrimitiveCreationKind::Cylinder,
                          quader::PrimitiveCreationKind::Cone,
                          quader::PrimitiveCreationKind::Sphere,
                          quader::PrimitiveCreationKind::Torus}) {
    SCOPED_TRACE(static_cast<int>(kind));
    ASSERT_TRUE(localization.select_locale("en-US"));
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
        quader::kPrimitiveTypeSetting, static_cast<double>(kind)).ok());
    quader::ModelingBoxPreview preview;
    preview.ok = preview.valid = true;
    preview.drag_state = quader::ModelingBoxDragState::Height;
    preview.corners = {quader::QVec3{-2, -2, -2}, quader::QVec3{-2, -2, 2},
                       quader::QVec3{2, -2, 2}, quader::QVec3{2, -2, -2},
                       quader::QVec3{-2, 2, -2}, quader::QVec3{-2, 2, 2},
                       quader::QVec3{2, 2, 2}, quader::QVec3{2, 2, -2}};
    preview.corner_count = 8;
    ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Primitive").ok);
    quader::QtViewportPanel panel;
    panel.attach_workspace(session.viewport_workspace());
    panel.set_localization(localization);
    panel.apply_ui_scale(quader::QtUiScaleMetrics{1.5f, 1u});
    auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
    ASSERT_NE(surface, nullptr);
    surface->resize(800, 600);
    auto *label = panel.findChild<QLabel *>(QStringLiteral("viewportPrimitiveFeedback_perspective"));
    ASSERT_NE(label, nullptr);
    ASSERT_TRUE(label->isHidden());
    QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(surface, &enter);
    QApplication::sendEvent(surface, &focus);
    auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
    const auto process = [&] {
      panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                          session.native_tools(), &session.point_placement(), 1.0 / 60.0);
    };
    const auto mouse = [&](QEvent::Type type, quader::QVec2 pixels,
                            Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
      const QPointF local{pixels.x / surface->devicePixelRatio(),
                           pixels.y / surface->devicePixelRatio()};
      QMouseEvent event(type, local, QPointF(surface->mapToGlobal(local.toPoint())),
                       type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                       type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                       modifiers);
      QApplication::sendEvent(surface, &event);
    };
    quader::PrimitiveCreationInteraction projection;
    const auto project = [&]() -> const quader::PrimitiveCreationControlsFrame & {
      return projection.project(quader::ViewportPrimitiveCreationApiAdapter::from_api(
          editor.tools().live_primitive_creation()), camera,
          panel.viewport_raster_scale(quader::ViewportPaneId::Perspective));
    };
    process();
    // The four-world-unit primitive is 128 map units tall with a 64-unit radius.
    for (const int index : {0, 2}) {
      const auto handle = project().controls[index];
      ASSERT_TRUE(handle.visible);
      mouse(QEvent::MouseMove, handle.screen_position);
      process();
      ASSERT_FALSE(label->isHidden());
      const bool torus = kind == quader::PrimitiveCreationKind::Torus;
      const QString name = index == 0
          ? (torus ? QStringLiteral("Outer Radius") : QStringLiteral("Radius"))
          : (torus ? QStringLiteral("Tube Height") : QStringLiteral("Height"));
      EXPECT_EQ(label->text(), name + (index == 0 ? QStringLiteral(": 64")
                                                 : QStringLiteral(": 128")));
    }
    const bool side_count_kind = kind == quader::PrimitiveCreationKind::Cylinder ||
        kind == quader::PrimitiveCreationKind::Cone;
    const int button_count = side_count_kind ? 1 : 2;
    for (int axis = 0; axis < button_count; ++axis) {
      const int index = 6 + axis;
      const auto button = project().controls[index];
      ASSERT_TRUE(button.visible);
      mouse(QEvent::MouseMove, button.screen_position);
      process();
      ASSERT_FALSE(label->isHidden());
      projection.hover(index);
      const auto &feedback = project();
      const auto prefix = QString::fromStdString(
          localization.text(feedback.label_key, "missing")) + QStringLiteral(": ");
      EXPECT_EQ(label->text(), prefix + QString::number(static_cast<int>(feedback.label_value)));
      const auto update_count = panel.primitive_feedback_update_count();
      for (int frame = 0; frame < 60; ++frame) process();
      EXPECT_EQ(panel.primitive_feedback_update_count(), update_count)
          << "Unchanged input must not reformat or reset the retained feedback";

      const auto before = editor.tools().live_primitive_creation();
      ASSERT_TRUE(before);
      const auto before_counts = counts(*before);
      const int setting_index = side_count_kind ? 0
          : kind == quader::PrimitiveCreationKind::Sphere ? 1 + axis : 3 + axis;
      for (const bool decrement : {false, true}) {
        mouse(QEvent::MouseButtonPress, button.screen_position,
              decrement ? Qt::ControlModifier : Qt::NoModifier);
        process();
        EXPECT_FALSE(label->isHidden());
        mouse(QEvent::MouseButtonRelease, button.screen_position);
        process();
        const auto after = editor.tools().live_primitive_creation();
        ASSERT_TRUE(after);
        auto expected_counts = before_counts;
        if (!decrement) ++expected_counts[setting_index];
        EXPECT_EQ(counts(*after), expected_counts);
        EXPECT_EQ(after->subdivisions, before->subdivisions);
        EXPECT_EQ(after->object_id, before->object_id);
        EXPECT_FLOAT_EQ(quader::length_squared(after->local_min - before->local_min), 0.0f);
        EXPECT_FLOAT_EQ(quader::length_squared(after->local_max - before->local_max), 0.0f);
        EXPECT_FALSE(after->resizing);
      }
      EXPECT_EQ(session.primitive_creation().shape_count(), 1);
    }
    // Catalog publication is an independent input to the presentation gate.
    mouse(QEvent::MouseMove, project().controls[6].screen_position);
    process();
    const auto english = label->text();
    const auto before_locale = panel.primitive_feedback_update_count();
    ASSERT_TRUE(localization.select_locale("it-IT"));
    process();
    EXPECT_NE(label->text(), english);
    EXPECT_EQ(panel.primitive_feedback_update_count(), before_locale + 1u);

    mouse(QEvent::MouseMove, {12, 12});
    process();
    EXPECT_TRUE(label->isHidden());
    mouse(QEvent::MouseMove, project().controls[6].screen_position);
    process();
    ASSERT_FALSE(label->isHidden());
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(surface, &leave);
    EXPECT_TRUE(label->isHidden());
    process();
    EXPECT_TRUE(label->isHidden());
    QApplication::sendEvent(surface, &enter);
    mouse(QEvent::MouseMove, project().controls[6].screen_position);
    process();
    ASSERT_FALSE(label->isHidden());
    QFocusEvent blur(QEvent::FocusOut);
    QApplication::sendEvent(surface, &blur);
    EXPECT_TRUE(label->isHidden());
    process();
    EXPECT_TRUE(label->isHidden());
    QApplication::sendEvent(surface, &focus);
    mouse(QEvent::MouseMove, project().controls[6].screen_position);
    process();
    ASSERT_FALSE(label->isHidden());
    panel.abandon_interactions();
    EXPECT_TRUE(label->isHidden());
    process();
    EXPECT_TRUE(label->isHidden());
  }
}

TEST(QtViewportSurfaceInputTests,
     InsertEdgeLoopRepeatsPhysicalClicksAndRetainsToolAcrossInterruptions) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-persistent-edge-loop-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Persistent loop input box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Back).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::InsertEdgeLoop).ok());
  session.history().clear();

  quader::QtViewportPanel panel;
  auto settings = quader::ViewportSettings::defaults();
  (void)settings.set_bool_value(quader::ViewportBoolSettingId::SnappingEnabled, false);
  panel.set_viewport_settings(settings);
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  camera.focus_origin(5);
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &enter);
  QApplication::sendEvent(surface, &focus);
  const auto process = [&] {
    panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  const auto point_at_height = [&](float height) {
    const auto projected = camera.project_world_point({1, height, 1});
    EXPECT_TRUE(projected.visible);
    return QPointF{projected.position.x / surface->devicePixelRatio(),
                   projected.position.y / surface->devicePixelRatio()};
  };
  const auto mouse = [&](QEvent::Type type, QPointF local) {
    QMouseEvent event(type, local, QPointF(surface->mapToGlobal(local.toPoint())),
                     type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                     type == QEvent::MouseButtonPress ? Qt::LeftButton : Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(surface, &event);
  };
  const auto expect_active = [&] {
    EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::InsertEdgeLoop);
    EXPECT_EQ(session.modeling().tool_status().active_tool,
              quader::ModelingViewportTool::InsertEdgeLoop);
  };
  const auto document = [&] {
    return *session.assets().mesh(box.value().mesh)->authored_polygon_document;
  };
  process();
  const auto original = document();
  for (int index = 0; index < 3; ++index) {
    SCOPED_TRACE(index);
    const auto local = point_at_height(-0.5f + 0.5f * static_cast<float>(index));
    mouse(QEvent::MouseMove, local);
    process();
    ASSERT_TRUE(session.modeling().tool_status().insert_edge_loop_has_preview);
    mouse(QEvent::MouseButtonPress, local);
    if (index != 1) process();
    // The middle insertion's complete click arrives between two input frames.
    mouse(QEvent::MouseButtonRelease, local);
    process();
    expect_active();
    EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_dragging);
    EXPECT_EQ(session.history().undo_count(), static_cast<std::size_t>(index + 1));
    EXPECT_EQ(document().vertices.size(), original.vertices.size() + 4u * (index + 1));
    process();
    EXPECT_EQ(session.history().undo_count(), static_cast<std::size_t>(index + 1))
        << "Trailing release and idle frames must not insert another loop";
  }
  const auto inserted = document();
  const auto hover = [&] {
    mouse(QEvent::MouseMove, point_at_height(0.75f));
    process();
    ASSERT_TRUE(session.modeling().tool_status().insert_edge_loop_has_preview);
  };
  hover();
  QKeyEvent escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  QApplication::sendEvent(surface, &escape_press);
  QApplication::sendEvent(surface, &escape_release);
  process();
  expect_active();
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));

  hover();
  QFocusEvent blur(QEvent::FocusOut);
  QApplication::sendEvent(surface, &blur);
  process();
  expect_active();
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));
  QApplication::sendEvent(surface, &focus);
  hover();
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(surface, &leave);
  QWidget outside;
  QMouseEvent outside_press(QEvent::MouseButtonPress, {10, 10}, {-100, -100},
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent outside_release(QEvent::MouseButtonRelease, {10, 10}, {-100, -100},
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&outside, &outside_press);
  QApplication::sendEvent(&outside, &outside_release);
  process();
  expect_active();
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));
  EXPECT_EQ(session.history().undo_count(), 3u);

  QApplication::sendEvent(surface, &enter);
  QApplication::sendEvent(surface, &focus);
  for (const int delta : {120, -120}) {
    const auto before_size = camera.state().orthographic_size;
    QWheelEvent wheel({400, 300}, {500, 400}, {}, {0, delta}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(surface, &wheel);
    process();
    if (delta > 0) EXPECT_LT(camera.state().orthographic_size, before_size);
    else EXPECT_GT(camera.state().orthographic_size, before_size);
    expect_active();
    EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));
    EXPECT_EQ(session.history().undo_count(), 3u);
  }
  hover();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  process();
  EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::Move);
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_EQ(session.history().undo_count(), 3u);
}

TEST(QtViewportSurfaceInputTests,
     RadialToolbarPreviewLocksOnClickAndWaitsForSettingsApply) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-radial-toolbar-preview-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  for (const bool coalesced : {false, true}) {
    SCOPED_TRACE(coalesced);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
    const auto source = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Radial toolbar source");
    ASSERT_TRUE(source.ok()) << source.error_message();
    ASSERT_TRUE(editor.scene().set_transform(source.value().entity, {.position = {6, 0, 0}}).ok());
    ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
    ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Top).ok());
    session.history().clear();

    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen, true);
    host.resize(900, 650);
    quader::QtViewportPanel panel(&host);
    panel.setGeometry(100, 0, 800, 600);
    panel.attach_workspace(session.viewport_workspace());
    QPushButton activate("Radial Array", &host);
    activate.setGeometry(0, 0, 95, 30);
    QObject::connect(&activate, &QPushButton::clicked, [&] {
      ASSERT_TRUE(editor.commands().execute(quader::quader_command::kToolsActivateRadialArray).ok());
    });
    host.show();
    QApplication::setActiveWindow(&host);
    activate.setFocus();
    ASSERT_EQ(QApplication::focusWidget(), &activate);
    activate.click();
    ASSERT_TRUE(session.radial_array().active());
    ASSERT_TRUE(editor.tools().set_setting("radial_array", "radial_array.copies", 3.0).ok());
    auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
    ASSERT_NE(surface, nullptr);
    surface->resize(800, 600);
    auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
    camera.focus_origin(16);
    const auto render_size = surface->render_size_pixels();
    camera.set_viewport_size(render_size.x, render_size.y);
    const auto camera_before = camera.pose();
    const auto frame = [&] {
      panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                          session.native_tools(), &session.point_placement(), 1.0 / 60.0);
    };
    const auto local_at = [&](quader::QVec3 world) {
      const auto projected = camera.project_world_point(world);
      EXPECT_TRUE(projected.visible);
      return QPointF{projected.position.x / surface->devicePixelRatio(),
                     projected.position.y / surface->devicePixelRatio()};
    };
    const auto mouse = [&](QEvent::Type type, QPointF local, Qt::MouseButton button,
                            Qt::MouseButtons held) {
      QMouseEvent event(type, local, QPointF(surface->mapToGlobal(local.toPoint())),
                       button, held, Qt::NoModifier);
      QApplication::sendEvent(surface, &event);
    };
    const auto origin = local_at({});
    QEnterEvent enter(origin, origin, QPointF(surface->mapToGlobal(origin.toPoint())));
    QApplication::sendEvent(surface, &enter);
    mouse(QEvent::MouseMove, origin, Qt::NoButton, Qt::NoButton);
    frame();
    auto &tool = session.radial_array();
    ASSERT_TRUE(tool.center_state().visible);
    ASSERT_FALSE(tool.center_locked());
    ASSERT_TRUE(tool.preview_state().active);
    ASSERT_EQ(tool.preview_state().sources.size(), 1u);
    ASSERT_EQ(tool.preview_state().sources.front().generated_world_transforms.size(), 2u);
    EXPECT_EQ(QApplication::focusWidget(), &activate)
        << "Hover placement must not take keyboard focus away from tool UI";
    EXPECT_FLOAT_EQ(quader::length_squared(camera.pose().eye - camera_before.eye), 0.0f);
    EXPECT_FLOAT_EQ(quader::length_squared(camera.pose().target - camera_before.target), 0.0f);
    EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
    EXPECT_EQ(session.history().undo_count(), 0u);
    const auto candidate = tool.center_state();

    // Settings and temporarily ineligible pointer frames retain the last preview.
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(surface, &leave);
    activate.setFocus();
    ASSERT_TRUE(editor.tools().set_setting("radial_array", "radial_array.copies", 4.0).ok());
    frame();
    ASSERT_TRUE(tool.preview_state().active);
    EXPECT_EQ(tool.preview_state().sources.front().generated_world_transforms.size(), 3u);
    EXPECT_FLOAT_EQ(quader::length_squared(tool.center_state().world_position -
                                          candidate.world_position), 0.0f);
    QApplication::sendEvent(surface, &enter);
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(surface, &focus);
    frame();
    for (const int delta : {120, -120}) {
      const auto copies_before = tool.copies();
      const auto zoom_before = camera.state().orthographic_size;
      QWheelEvent wheel(origin, QPointF(surface->mapToGlobal(origin.toPoint())),
                        {}, {0, delta}, Qt::NoButton, Qt::ShiftModifier,
                        Qt::NoScrollPhase, false);
      QApplication::sendEvent(surface, &wheel);
      frame();
      EXPECT_EQ(tool.copies(), delta > 0 ? copies_before + 1u : copies_before - 1u);
      EXPECT_FLOAT_EQ(camera.state().orthographic_size, zoom_before);
      EXPECT_TRUE(tool.preview_state().active);
    }
    for (const int delta : {120, -120}) {
      const auto repeats_before = tool.repeats();
      const auto copies_before = tool.copies();
      const auto zoom_before = camera.state().orthographic_size;
      QWheelEvent wheel(origin, QPointF(surface->mapToGlobal(origin.toPoint())),
                        {}, {0, delta}, Qt::NoButton, Qt::ControlModifier,
                        Qt::NoScrollPhase, false);
      QApplication::sendEvent(surface, &wheel);
      frame();
      EXPECT_EQ(tool.repeats(), delta > 0 ? repeats_before + 1u : repeats_before - 1u);
      EXPECT_EQ(tool.copies(), copies_before);
      EXPECT_FLOAT_EQ(camera.state().orthographic_size, zoom_before);
      ASSERT_TRUE(tool.preview_state().active);
      EXPECT_EQ(tool.preview_state().sources.front().generated_world_transforms.size(),
                tool.copies() * tool.repeats() - 1u);
    }
    const auto zoom_before_plain_wheel = camera.state().orthographic_size;
    QWheelEvent plain_wheel(origin, QPointF(surface->mapToGlobal(origin.toPoint())),
                            {}, {0, 120}, Qt::NoButton, Qt::NoModifier,
                            Qt::NoScrollPhase, false);
    QApplication::sendEvent(surface, &plain_wheel);
    frame();
    EXPECT_LT(camera.state().orthographic_size, zoom_before_plain_wheel);
    EXPECT_EQ(tool.copies(), 4u);
    EXPECT_EQ(tool.repeats(), 1u);
    EXPECT_TRUE(tool.preview_state().active);
    ASSERT_FALSE(tool.create_instances());
    for (const bool expected_instances : {true, false}) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_I, Qt::NoModifier);
      QApplication::sendEvent(surface, &press);
      QApplication::sendEvent(surface, &release);
      frame();
      EXPECT_EQ(tool.create_instances(), expected_instances);
      EXPECT_TRUE(tool.preview_state().active);
    }
    const auto before_navigation = tool.center_state().world_position;
    mouse(QEvent::MouseButtonPress, origin, Qt::MiddleButton, Qt::MiddleButton);
    frame();
    mouse(QEvent::MouseMove, origin + QPointF{20, 10}, Qt::NoButton, Qt::MiddleButton);
    frame();
    EXPECT_TRUE(panel.command_input_context().navigation_active);
    EXPECT_TRUE(tool.preview_state().active);
    EXPECT_FLOAT_EQ(quader::length_squared(tool.center_state().world_position -
                                          before_navigation), 0.0f);
    mouse(QEvent::MouseButtonRelease, origin + QPointF{20, 10}, Qt::MiddleButton, Qt::NoButton);
    frame();
    camera.focus_origin(16);
    camera.view_axis(quader::ViewportViewAxis::Y, 1);
    frame();

    const auto placement = local_at({2, 0, 2});
    mouse(QEvent::MouseMove, placement, Qt::NoButton, Qt::NoButton);
    frame();
    ASSERT_TRUE(tool.preview_state().active);
    const auto placed_center = tool.center_state().world_position;
    const auto placed_preview = tool.preview_state().sources.front().generated_world_transforms;
    ASSERT_EQ(placed_preview.size(), 3u);
    const auto content_before_placement = session.scene().content_revision();
    mouse(QEvent::MouseButtonPress, placement, Qt::LeftButton, Qt::LeftButton);
    if (!coalesced) frame();
    mouse(QEvent::MouseButtonRelease, placement, Qt::LeftButton, Qt::NoButton);
    // A newer hover position cannot move the center belonging to the queued click.
    mouse(QEvent::MouseMove, local_at({-2, 0, -2}), Qt::NoButton, Qt::NoButton);
    frame();
    ASSERT_TRUE(tool.active());
    ASSERT_TRUE(tool.center_locked());
    ASSERT_TRUE(tool.preview_state().active);
    EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::RadialArray);
    EXPECT_FLOAT_EQ(quader::length_squared(tool.center_state().world_position - placed_center), 0.0f);
    ASSERT_EQ(tool.preview_state().sources.front().generated_world_transforms.size(), placed_preview.size());
    for (std::size_t index = 0; index < placed_preview.size(); ++index) {
      EXPECT_EQ(tool.preview_state().sources.front().generated_world_transforms[index].values,
                placed_preview[index].values);
    }
    frame();
    EXPECT_EQ(session.scene().content_revision(), content_before_placement);
    EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
    EXPECT_EQ(session.history().undo_count(), 0u);

    const auto locked_preview_revision = tool.preview_state().revision;
    ASSERT_TRUE(editor.tools().set_setting("radial_array", "radial_array.copies", 5.0).ok());
    ASSERT_TRUE(editor.tools().set_setting("radial_array", "radial_array.degrees", 180.0).ok());
    frame();
    ASSERT_TRUE(tool.center_locked());
    ASSERT_TRUE(tool.preview_state().active);
    EXPECT_GT(tool.preview_state().revision, locked_preview_revision);
    EXPECT_FLOAT_EQ(quader::length_squared(tool.center_state().world_position - placed_center), 0.0f);
    const auto expected = tool.preview_state().sources.front().generated_world_transforms;
    ASSERT_EQ(expected.size(), 4u);
    EXPECT_EQ(session.scene().content_revision(), content_before_placement);
    EXPECT_EQ(session.history().undo_count(), 0u);

    // Use the real retained Apply button after editing the locked preview.
    quader::QtToolSettingsPanel settings(&host);
    settings.bind(editor);
    settings.refresh();
    auto *apply = settings.findChild<QPushButton *>(QStringLiteral("tool_settings.apply"));
    ASSERT_NE(apply, nullptr);
    ASSERT_TRUE(apply->isEnabled());
    apply->click();
    frame();
    EXPECT_FALSE(tool.has_session());
    EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::Select);
    EXPECT_EQ(session.history().undo_count(), 1u);
    EXPECT_EQ(editor.query().history().undo_label, "Radial Array");
    std::vector<quader::EntityId> generated;
    for (const auto &object : editor.query().modeling_objects()) {
      if (object.entity != source.value().entity) generated.push_back(object.entity);
    }
    std::sort(generated.begin(), generated.end());
    ASSERT_EQ(generated.size(), expected.size());
    for (std::size_t index = 0; index < generated.size(); ++index) {
      const auto actual = session.scene().world_transform_matrix(generated[index]);
      for (std::size_t component = 0; component < actual.values.size(); ++component) {
        EXPECT_NEAR(actual.values[component], expected[index].values[component], 0.0001f);
      }
    }
    frame();
    EXPECT_EQ(session.history().undo_count(), 1u);
    EXPECT_EQ(editor.query().modeling_objects().size(), 5u);
  }
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
     FocusLossCancelsDraftCurveConstructionAndLeavesSelectedControlsInert) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-curve-focus-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const bool drawing : {false, true}) {
    SCOPED_TRACE(drawing ? "new draft" : "existing curve");
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto created = editor.modeling().create_curve({
        .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                   {.position = {2.0f, 0.0f, 0.0f}}},
    });
    ASSERT_TRUE(created.ok()) << created.error_message();
    if (!drawing) {
      ASSERT_TRUE(editor.modeling().set_curve_closed(created.value(), true).ok());
    }
    ASSERT_TRUE(editor.history().undo().ok());
    ASSERT_TRUE(editor.history().can_redo());
    ASSERT_TRUE(editor.tools().activate(quader::kCurveToolId).ok());
    session.mark_clean();
    quader::CurveToolService &curve = session.curve();
    const auto before = *curve.preview_state().document;
    const auto history_before = editor.history().snapshot();

    quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
    surface.resize(800, 600);
    quader::ViewportCameraController camera;
    const auto size = surface.render_size_pixels();
    camera.set_viewport_size(size.x, size.y);
    camera.focus_origin(16.0f);
    camera.view_axis(quader::ViewportViewAxis::Y, 1);
    quader::ViewportInteractionController interaction;
    quader::ViewportInputRouter input_router;
    input_router.set_active_tool(quader::ViewportToolId::Curve);
    quader::ViewportSettings viewport_settings;
    quader::ViewportInputResult input_result;
    const auto dispatch = [&](const quader::QtViewportInputFrame &sample) {
      const auto native = interaction.handle_native_tools(
          editor, session.scene(), session.assets(), session.native_tools(),
          &session.point_placement(), camera,
          {.input = sample.input,
           .input_result = input_result,
           .left_pressed = sample.left_pressed,
           .left_down = sample.left_down,
           .left_released = sample.left_released,
           .left_click_local = sample.left_click_local});
      interaction.handle_after_active_tool(
          editor,
          {
              .input_router = input_router,
              .camera = camera,
              .settings = viewport_settings,
              .input = sample.input,
              .input_result = input_result,
              .left_click_local = sample.left_click_local,
              .left_pressed = sample.left_pressed,
              .left_down = sample.left_down,
              .left_released = sample.left_released,
              .active_tool_allows_selection_fallback = false,
              .selection_state =
                  {
                      .summary = editor.selection().summary(),
                      .scene_revision = session.scene().revision(),
                      .asset_revision = session.assets().revision(),
                  },
          },
          {}, {}, session.native_tools(), nullptr);
      return native;
    };
    const quader::QVec2 press_pixels = drawing
        ? quader::QVec2{size.x * 0.5f, size.y * 0.5f}
        : camera.project_world_point(quader::transform_point(
              curve.preview_state().world_transform,
              quader::curve_control_local_position(
                  before.points()[0], quader::CurveControlKind::Anchor)))
              .position;
    const qreal scale = surface.devicePixelRatio();
    const QPointF press_local{press_pixels.x / scale, press_pixels.y / scale};
    const QPointF press_global = press_local + QPointF{100.0, 100.0};
    QMouseEvent press(QEvent::MouseButtonPress, press_local, press_global,
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&surface, &press);
    const auto pressed = surface.take_input_frame(0.0f, true);
    EXPECT_FALSE(pressed.input.pointer_interaction_canceled);
    const auto press_result = dispatch(pressed);
    if (drawing) {
      EXPECT_TRUE(press_result.claim_interaction_pane);
      ASSERT_TRUE(curve.point_construction_dragging());
    } else {
      ASSERT_FALSE(curve.dragging());
      EXPECT_TRUE(curve.control_selection().empty())
          << "Curve click selection must publish on release.";

      QMouseEvent click_release(QEvent::MouseButtonRelease, press_local,
                                press_global, Qt::LeftButton, Qt::NoButton,
                                Qt::NoModifier);
      QApplication::sendEvent(&surface, &click_release);
      const auto clicked = surface.take_input_frame(0.0f, true);
      ASSERT_TRUE(clicked.left_released);
      (void)dispatch(clicked);
      const quader::CurveControlTarget anchor{
          .point = before.points()[0].id,
          .kind = quader::CurveControlKind::Anchor,
      };
      EXPECT_EQ(curve.control_selection().controls,
                std::vector<quader::CurveControlTarget>{anchor});
      EXPECT_EQ(curve.control_selection().active, anchor);

      QMouseEvent drag_press(QEvent::MouseButtonPress, press_local,
                             press_global, Qt::LeftButton, Qt::LeftButton,
                             Qt::NoModifier);
      QApplication::sendEvent(&surface, &drag_press);
      const auto drag_pressed = surface.take_input_frame(0.0f, true);
      ASSERT_TRUE(drag_pressed.left_pressed);
      (void)dispatch(drag_pressed);
      EXPECT_EQ(curve.control_selection().controls,
                std::vector<quader::CurveControlTarget>{anchor});
      EXPECT_EQ(curve.control_selection().active, anchor);
    }

    const QPointF moved_local = press_local + QPointF{80.0, 0.0};
    QMouseEvent move(QEvent::MouseMove, moved_local,
                      press_global + QPointF{80.0, 0.0},
                      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&surface, &move);
    (void)dispatch(surface.take_input_frame(0.0f, true));
    if (drawing) {
      ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));
    } else {
      ASSERT_TRUE(curve.preview_state().document->has_same_geometry(before));
      ASSERT_EQ(curve.control_selection().controls.size(), 1u);
      ASSERT_TRUE(curve.control_selection().active.has_value());
    }

    QFocusEvent focus_out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QApplication::sendEvent(&surface, &focus_out);
    const auto lost_focus = surface.take_input_frame(0.0f, true);
    // The real Qt path retains its legacy release and forced pane focus. Only
    // the independent cancellation edge distinguishes this from button-up.
    EXPECT_TRUE(lost_focus.left_released);
    EXPECT_FALSE(lost_focus.left_down);
    EXPECT_TRUE(lost_focus.input.viewport_focused);
    EXPECT_TRUE(lost_focus.input.viewport_hovered);
    EXPECT_TRUE(lost_focus.input.pointer_interaction_canceled);
    EXPECT_TRUE(lost_focus.pointer_activity());
    EXPECT_TRUE(dispatch(lost_focus).curve_consumed);
    EXPECT_FALSE(session.native_tools().pointer_dragging());
    EXPECT_FALSE(curve.dragging());
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    if (drawing) {
      EXPECT_TRUE(curve.control_selection().empty());
      EXPECT_FALSE(curve.control_selection().active.has_value());
    } else {
      const quader::CurveControlTarget anchor{
          .point = before.points()[0].id,
          .kind = quader::CurveControlKind::Anchor,
      };
      EXPECT_EQ(curve.control_selection().controls,
                std::vector<quader::CurveControlTarget>{anchor});
      ASSERT_TRUE(curve.control_selection().active.has_value());
      EXPECT_EQ(curve.control_selection().active, anchor);
    }
    EXPECT_FALSE(session.dirty());
    expect_history_unchanged(editor.history().snapshot(), history_before);

    const auto next = surface.take_input_frame(0.0f, true);
    EXPECT_FALSE(next.input.pointer_interaction_canceled);
    EXPECT_FALSE(next.left_released);
    QMouseEvent release(QEvent::MouseButtonRelease, moved_local,
                         press_global + QPointF{80.0, 0.0},
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&surface, &release);
    const auto released = surface.take_input_frame(0.0f, true);
    EXPECT_FALSE(released.input.pointer_interaction_canceled);
    (void)dispatch(released);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    expect_history_unchanged(editor.history().snapshot(), history_before);
  }
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
  ASSERT_EQ(press_frame.input.pointer_pressed_edges.size(), 1u);
  EXPECT_EQ(press_frame.input.pointer_pressed_edges.front().button,
            QuaderMouseButton::Right);
  EXPECT_FLOAT_EQ(press_frame.input.pointer_pressed_edges.front().local.x,
                  static_cast<float>(press_local.x() * surface.devicePixelRatio()));
  EXPECT_FLOAT_EQ(press_frame.input.pointer_pressed_edges.front().local.y,
                  static_cast<float>(press_local.y() * surface.devicePixelRatio()));
  EXPECT_NE(press_frame.input.pointer_pressed_edges.front().sequence, 0u);
  EXPECT_TRUE(has_right_gesture(press_frame, QuaderInputGesture::Drag));
  EXPECT_FALSE(has_right_gesture(press_frame, QuaderInputGesture::Click));

  QMouseEvent stationary_release(
      QEvent::MouseButtonRelease, press_local, press_global,
      Qt::RightButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &stationary_release);
  const quader::QtViewportInputFrame click_frame =
      surface.take_input_frame(0.0f, false);
  ASSERT_EQ(click_frame.input.pointer_released_edges.size(), 1u);
  EXPECT_EQ(click_frame.input.pointer_released_edges.front().button,
            QuaderMouseButton::Right);
  EXPECT_GT(click_frame.input.pointer_released_edges.front().sequence,
            press_frame.input.pointer_pressed_edges.front().sequence);
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
  const auto drag_move_frame = surface.take_input_frame(0.0f, false);
  ASSERT_EQ(drag_move_frame.input.pointer_drag_started_edges.size(), 1u);
  EXPECT_EQ(drag_move_frame.input.pointer_drag_started_edges.front().button,
            QuaderMouseButton::Right);

  QMouseEvent drag_release(QEvent::MouseButtonRelease, drag_local, drag_global,
                           Qt::RightButton, Qt::NoButton, modifiers);
  QApplication::sendEvent(&surface, &drag_release);
  const quader::QtViewportInputFrame drag_release_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_FALSE(has_right_gesture(drag_release_frame,
                                 QuaderInputGesture::Click));
}

TEST(QtViewportSurfaceInputTests,
     NativeMeshDoubleClicksCycleDepthThroughTheProductionPanelPath) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-depth-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Back).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  std::array<quader::EntityId, 3> objects{};
  for (std::size_t index = 0; index < objects.size(); ++index) {
    const float z = 3.0f - 3.0f * static_cast<float>(index);
    const auto box = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, z - 1.0f},
         .max = {1.0f, 1.0f, z + 1.0f}},
        "Depth input box");
    ASSERT_TRUE(box.ok()) << box.error_message();
    objects[index] = box.value().entity;
  }
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());

  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);

  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  const auto send_mouse = [&](QEvent::Type type, Qt::MouseButtons buttons) {
    QMouseEvent event(type, local, global, Qt::LeftButton, buttons,
                       Qt::NoModifier);
    QApplication::sendEvent(surface, &event);
    process_input();
  };
  process_input();
  ASSERT_EQ(panel.tool_state().selection_mode,
            quader::ViewportSelectionMode::Object);
  ASSERT_EQ(session.viewport_workspace()
                .camera(quader::ViewportPaneId::Perspective)
                .state().projection,
            quader::ViewportProjection::Orthographic);

  for (const auto expected : {objects[1], objects[2], objects[0]}) {
    const auto selected_before = session.scene().selected_entities();
    const auto active_before = session.scene().primary_selection();
    const auto selection_revision_before = session.scene().selection_revision();
    const auto history_before = editor.history().snapshot();
    send_mouse(QEvent::MouseButtonPress, Qt::LeftButton);
    send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
    EXPECT_EQ(session.scene().selected_entities(), selected_before);
    EXPECT_EQ(session.scene().primary_selection(), active_before);
    EXPECT_EQ(session.scene().selection_revision(), selection_revision_before);
    expect_history_unchanged(editor.history().snapshot(), history_before);
    send_mouse(QEvent::MouseButtonDblClick, Qt::LeftButton);
    send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
    EXPECT_EQ(session.scene().primary_selection(), expected);
    EXPECT_EQ(session.scene().selection_revision(),
              selection_revision_before + 1u);
    process_input();
    EXPECT_EQ(session.scene().primary_selection(), expected);
    EXPECT_EQ(session.scene().selection_revision(),
              selection_revision_before + 1u);
    ASSERT_TRUE(editor.history().undo().ok());
    EXPECT_EQ(session.scene().selected_entities(), selected_before);
    EXPECT_EQ(session.scene().primary_selection(), active_before);
    ASSERT_TRUE(editor.history().redo().ok());
    EXPECT_EQ(session.scene().selected_entities(),
              std::vector<quader::EntityId>{expected});
  }
}

TEST(QtViewportSurfaceInputTests,
     NativeMeshDoubleClickCyclesEdgeOnBoundariesWithCoalescedEvents) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-boundary-depth-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::Move}) {
    for (const bool coalesced : {false, true}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(coalesced);
      quader::SceneSession session;
      quader::QuaderEditorApi editor(
          quader::QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Left).ok());
      ASSERT_TRUE(editor.tools().activate(tool).ok());
      quader_poly::Document topology;
      const std::array vertices{
          quader_poly::add_vertex(topology, {-2.0F, -1.0F, 0.0F}),
          quader_poly::add_vertex(topology, {2.0F, -1.0F, 0.0F}),
          quader_poly::add_vertex(topology, {2.0F, 1.0F, 0.0F}),
          quader_poly::add_vertex(topology, {-2.0F, 1.0F, 0.0F})};
      ASSERT_NE(quader_poly::add_face(topology, vertices),
                quader_poly::kInvalidElementId);
      const auto profile = editor.modeling().create_mesh(
          quader::QuaderPolygonDocumentInternalAccess::from_backend(
              std::move(topology)));
      ASSERT_TRUE(profile.ok()) << profile.error_message();
      const auto room = editor.modeling().create_box(
          {.min = {-5.0F, -5.0F, -5.0F}, .max = {5.0F, 5.0F, 5.0F}});
      ASSERT_TRUE(room.ok()) << room.error_message();
      ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object)
                      .ok());
      ASSERT_TRUE(editor.selection().select_object(room.value()).ok());
      ASSERT_TRUE(editor.commands()
                      .execute(quader::quader_command::kModelingFlipFaceNormals)
                      .ok());
      ASSERT_TRUE(editor.selection().clear().ok());

      quader::ViewportSettings settings = quader::ViewportSettings::defaults();
      (void)settings.set_bool_value(
          quader::ViewportBoolSettingId::BackfaceSelection, false);
      quader::QtViewportPanel panel;
      panel.set_viewport_settings(settings);
      panel.attach_workspace(session.viewport_workspace());
      auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
      ASSERT_NE(surface, nullptr);
      surface->resize(800, 600);
      const QPointF local{400.0, 300.0};
      const QPointF global{500.0, 400.0};
      QEnterEvent enter(local, local, global);
      QApplication::sendEvent(surface, &enter);
      const auto process_input = [&]() {
        panel.process_input(
            editor, session.scene(), session.assets(), session.modeling(), {},
            session.native_tools(), &session.point_placement(), 1.0 / 60.0);
      };
      const auto send_mouse = [&](QEvent::Type type, Qt::MouseButtons buttons) {
        QMouseEvent event(type, local, global, Qt::LeftButton, buttons,
                           Qt::NoModifier);
        QApplication::sendEvent(surface, &event);
        if (!coalesced) {
          process_input();
        }
      };
      process_input();
      for (const auto expected : {profile.value().entity, room.value().entity,
                                  profile.value().entity}) {
        const auto selected_before = session.scene().selected_entities();
        const auto active_before = session.scene().primary_selection();
        const auto selection_revision_before = session.scene().selection_revision();
        const auto history_before = editor.history().snapshot();
        send_mouse(QEvent::MouseButtonPress, Qt::LeftButton);
        send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
        EXPECT_EQ(session.scene().selected_entities(), selected_before);
        EXPECT_EQ(session.scene().primary_selection(), active_before);
        EXPECT_EQ(session.scene().selection_revision(), selection_revision_before);
        expect_history_unchanged(editor.history().snapshot(), history_before);
        send_mouse(QEvent::MouseButtonDblClick, Qt::LeftButton);
        send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
        process_input();
        EXPECT_EQ(session.scene().primary_selection(), expected);
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + 1u);
        process_input();
        EXPECT_EQ(session.scene().primary_selection(), expected);
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + 1u);
        EXPECT_FALSE(session.modeling().transform_drag_active());
      }
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     ModifiedMeshDoubleClicksEditOnlyTheDepthHitAcrossNativeEventBatches) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-modified-depth-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  // These schedules also exercise a first release queued with the second press.
  const std::array frame_schedules{
      std::array{true, true, true, true},
      std::array{false, false, false, true},
      std::array{true, false, true, true}};
  for (const auto tool : {quader::ViewportToolId::Select,
                          quader::ViewportToolId::Move}) {
    for (std::size_t schedule = 0; schedule < frame_schedules.size(); ++schedule) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(schedule);
      quader::SceneSession session;
      quader::QuaderEditorApi editor(
          quader::QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Back).ok());
      ASSERT_TRUE(editor.tools().activate(tool).ok());
      const auto front = editor.modeling().create_box(
          {.min = {-1.0F, -1.0F, 2.0F}, .max = {1.0F, 1.0F, 4.0F}},
          "Front depth input box");
      const auto behind = editor.modeling().create_box(
          {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
          "Behind depth input box");
      const auto unrelated = editor.modeling().create_box(
          {.min = {5.0F, 5.0F, -1.0F}, .max = {7.0F, 7.0F, 1.0F}},
          "Unrelated selected box");
      ASSERT_TRUE(front.ok()) << front.error_message();
      ASSERT_TRUE(behind.ok()) << behind.error_message();
      ASSERT_TRUE(unrelated.ok()) << unrelated.error_message();
      ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object)
                      .ok());
      ASSERT_TRUE(editor.selection().select_object(unrelated.value()).ok());
      session.history().clear();
      const auto scene_content_revision = session.scene().content_revision();
      const auto transform_revision = session.scene().transform_revision();
      const auto modeling_content_revision = session.modeling().content_revision();

      quader::QtViewportPanel panel;
      panel.attach_workspace(session.viewport_workspace());
      auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
      ASSERT_NE(surface, nullptr);
      surface->resize(800, 600);
      const QPointF local{400.0, 300.0};
      const QPointF global{500.0, 400.0};
      QEnterEvent enter(local, local, global);
      QApplication::sendEvent(surface, &enter);
      QFocusEvent focus(QEvent::FocusIn);
      QApplication::sendEvent(surface, &focus);
      const auto process_input = [&]() {
        panel.process_input(
            editor, session.scene(), session.assets(), session.modeling(), {},
            session.native_tools(), &session.point_placement(), 1.0 / 60.0);
      };
      const auto double_click = [&](Qt::KeyboardModifiers modifiers,
                                    bool selection_changes = true) {
        const auto selected_before = session.scene().selected_entities();
        const auto active_before = session.scene().primary_selection();
        const auto selection_revision_before = session.scene().selection_revision();
        const auto history_before = editor.history().snapshot();
        constexpr std::array kEventTypes{
            QEvent::MouseButtonPress, QEvent::MouseButtonRelease,
            QEvent::MouseButtonDblClick, QEvent::MouseButtonRelease};
        for (std::size_t index = 0; index < kEventTypes.size(); ++index) {
          const bool release = kEventTypes[index] == QEvent::MouseButtonRelease;
          // Selection intent belongs to the double-click event, even when the
          // modifier has already been released before the next input frame.
          QMouseEvent event(kEventTypes[index], local, global, Qt::LeftButton,
                            release ? Qt::NoButton : Qt::LeftButton,
                            release ? Qt::NoModifier : modifiers);
          QApplication::sendEvent(surface, &event);
          if (frame_schedules[schedule][index]) {
            process_input();
          }
          if (index < 2u) {
            EXPECT_EQ(session.scene().selected_entities(), selected_before);
            EXPECT_EQ(session.scene().primary_selection(), active_before);
            EXPECT_EQ(session.scene().selection_revision(),
                      selection_revision_before);
            expect_history_unchanged(editor.history().snapshot(), history_before);
          }
        }
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + (selection_changes ? 1u : 0u));
        if (!selection_changes) {
          expect_history_unchanged(editor.history().snapshot(), history_before);
        }
      };
      const auto expect_selection =
          [&](std::initializer_list<quader::EntityId> expected_entities) {
            std::vector<quader::EntityId> expected(expected_entities);
            std::sort(expected.begin(), expected.end());
            const auto selection_revision = session.scene().selection_revision();
            for (int idle_frame = 0; idle_frame < 2; ++idle_frame) {
              auto selected = session.scene().selected_entities();
              std::sort(selected.begin(), selected.end());
              EXPECT_EQ(selected, expected);
              EXPECT_EQ(session.scene().selection_revision(), selection_revision);
              EXPECT_NE(std::find(selected.begin(), selected.end(),
                                  session.scene().primary_selection()),
                        selected.end());
              EXPECT_FALSE(session.modeling().transform_drag_active());
              EXPECT_EQ(editor.history().snapshot().undo_label, "Select Entities");
              EXPECT_FALSE(editor.history().can_redo());
              EXPECT_EQ(session.scene().content_revision(),
                        scene_content_revision);
              EXPECT_EQ(session.scene().transform_revision(), transform_revision);
              EXPECT_EQ(session.modeling().content_revision(),
                        modeling_content_revision);
              process_input();
            }
          };
      process_input();

      double_click(Qt::ShiftModifier);
      expect_selection({unrelated.value().entity, behind.value().entity});

      ASSERT_TRUE(editor.selection()
                      .select_object(front.value(), quader::QuaderSelectionEdit::Add)
                      .ok());
      double_click(Qt::ControlModifier);
      expect_selection({unrelated.value().entity, front.value().entity});

      // Further Remove gestures advance through the stack. An already removed
      // mesh stays unselected when the traversal wraps back to it.
      double_click(Qt::ControlModifier);
      expect_selection({unrelated.value().entity});
      double_click(Qt::ControlModifier, false);
      expect_selection({unrelated.value().entity});

      // Changing the operation restarts at the first mesh behind the front hit.
      double_click(Qt::ShiftModifier);
      expect_selection({unrelated.value().entity, behind.value().entity});
      const auto selected_before_undo = session.scene().selected_entities();
      const auto active_before_undo = session.scene().primary_selection();
      ASSERT_TRUE(editor.history().undo().ok());
      EXPECT_EQ(session.scene().selected_entities(),
                std::vector<quader::EntityId>{unrelated.value().entity});
      EXPECT_EQ(session.scene().primary_selection(), unrelated.value().entity);
      ASSERT_TRUE(editor.history().redo().ok());
      EXPECT_EQ(session.scene().selected_entities(), selected_before_undo);
      EXPECT_EQ(session.scene().primary_selection(), active_before_undo);
      expect_selection({unrelated.value().entity, behind.value().entity});
    }
  }
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
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
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
  const quader::ViewportCameraPose camera_before_click =
      session.viewport_workspace()
          .camera(quader::ViewportPaneId::Perspective)
          .pose();
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
  const quader::ViewportCameraPose camera_after_click =
      session.viewport_workspace()
          .camera(quader::ViewportPaneId::Perspective)
          .pose();
  EXPECT_EQ(quader::viewport_camera_pose_persistence_signature(
                camera_after_click),
            quader::viewport_camera_pose_persistence_signature(
                camera_before_click));
}

TEST(QtViewportSurfaceInputTests,
     PhysicalWheelAndMiddleDragRepairStaleDepthThroughProductionPanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-navigation-depth-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -6.0f}, .max = {1.0f, 1.0f, -4.0f}});
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());

  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  quader::QtViewportSurface *surface =
      panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  const QPointF center{400.0, 300.0};
  const QPointF center_global{500.0, 400.0};
  QEnterEvent enter(center, center, center_global);
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);

  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  quader::ViewportCameraController &camera =
      session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  const auto restore_stale_pose = [&]() {
    ASSERT_TRUE(camera.restore_perspective_pose({
        .eye = {0.0f, 0.0f, 0.0f},
        .target = {0.0f, 0.0f, -158.0f},
        .up = {0.0f, 1.0f, 0.0f},
    }));
  };
  restore_stale_pose();
  process_input();

  // Plain wheel repair uses the complete scene depth (the scene-root origin
  // plus this box produces a depth of three), rather than either the surface
  // under the cursor or the stale target 158 units away.
  QWheelEvent wheel_in(center, center_global, {}, QPoint{0, 120}, Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(surface, &wheel_in);
  process_input();
  EXPECT_NEAR(camera.state().distance, 3.0f / 1.18f, 0.01f);

  restore_stale_pose();
  const quader::ViewportScreenPoint anchor_before =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(anchor_before.visible);

  QMouseEvent middle_press(QEvent::MouseButtonPress, center, center_global,
                           Qt::MiddleButton, Qt::MiddleButton,
                           Qt::NoModifier);
  QApplication::sendEvent(surface, &middle_press);
  process_input();
  const quader::ViewportCameraPose before_orbit = camera.pose();
  EXPECT_NEAR(camera.state().distance, 158.0f, 0.01f);

  const QPointF moved{405.0, 300.0};
  const QPointF moved_global{505.0, 400.0};
  QMouseEvent middle_move(QEvent::MouseMove, moved, moved_global, Qt::NoButton,
                          Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &middle_move);
  process_input();

  EXPECT_TRUE(panel.command_input_context().navigation_active);
  EXPECT_TRUE(panel.command_input_context().orbit_navigation_active);
  EXPECT_FALSE(panel.command_input_context().pan_navigation_active);

  const QPointF moved_again{410.0, 300.0};
  const QPointF moved_again_global{510.0, 400.0};
  QMouseEvent middle_move_again(QEvent::MouseMove, moved_again,
                                moved_again_global, Qt::NoButton,
                                Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &middle_move_again);
  QMouseEvent middle_release(QEvent::MouseButtonRelease, moved_again,
                             moved_again_global, Qt::MiddleButton,
                             Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &middle_release);
  const QPointF post_release{700.0, 300.0};
  const QPointF post_release_global{800.0, 400.0};
  QMouseEvent post_release_move(QEvent::MouseMove, post_release,
                                post_release_global, Qt::NoButton,
                                Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &post_release_move);
  process_input();

  const quader::ViewportCameraPose after_orbit = camera.pose();
  EXPECT_FALSE(panel.command_input_context().navigation_active);
  EXPECT_NEAR(camera.state().distance, 4.0f, 0.01f);
  const quader::ViewportScreenPoint anchor_after =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(anchor_after.visible);
  EXPECT_NEAR(anchor_after.position.x, anchor_before.position.x, 0.05f);
  EXPECT_NEAR(anchor_after.position.y, anchor_before.position.y, 0.05f);
  EXPECT_NEAR(
      std::acos(std::clamp(
          quader::dot(quader::normalize(before_orbit.target - before_orbit.eye),
                      quader::normalize(after_orbit.target - after_orbit.eye)),
          -1.0f, 1.0f)),
      10.0f * static_cast<float>(
                 quader::viewport_host_defaults().orbit_radians_per_pixel),
      0.0005f);
  const float expected_orbit_angle =
      10.0f * static_cast<float>(
                  quader::viewport_host_defaults().orbit_radians_per_pixel);
  EXPECT_LE(quader::length(after_orbit.eye - before_orbit.eye),
            2.0f * quader::length(before_orbit.eye -
                                  quader::QVec3{0.0f, 0.0f, -4.0f}) *
                    std::sin(expected_orbit_angle * 0.5f) +
                0.001f);

  // Exercise the user's exact production sequence: Shift+MMB pan, reciprocal
  // wheel zoom, then a fresh small MMB orbit. Every gesture must use its own
  // pointer baseline and the visible depth at its press.
  ASSERT_TRUE(camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 0.0f},
      .target = {0.0f, 0.0f, -5.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const quader::ViewportCameraPose before_pan = camera.pose();
  QMouseEvent pan_press(QEvent::MouseButtonPress, center, center_global,
                        Qt::MiddleButton, Qt::MiddleButton,
                        Qt::ShiftModifier);
  QApplication::sendEvent(surface, &pan_press);
  process_input();
  const QPointF panned{430.0, 310.0};
  const QPointF panned_global{530.0, 410.0};
  QMouseEvent pan_move(QEvent::MouseMove, panned, panned_global, Qt::NoButton,
                       Qt::MiddleButton, Qt::ShiftModifier);
  QApplication::sendEvent(surface, &pan_move);
  process_input();
  EXPECT_TRUE(panel.command_input_context().navigation_active);
  EXPECT_TRUE(panel.command_input_context().pan_navigation_active);
  EXPECT_FALSE(panel.command_input_context().orbit_navigation_active);
  QMouseEvent pan_release(QEvent::MouseButtonRelease, panned, panned_global,
                          Qt::MiddleButton, Qt::NoButton,
                          Qt::ShiftModifier);
  QApplication::sendEvent(surface, &pan_release);
  process_input();
  const quader::ViewportCameraPose after_pan = camera.pose();
  EXPECT_FALSE(panel.command_input_context().navigation_active);
  EXPECT_GT(quader::length(after_pan.target - before_pan.target), 0.01f);

  const float distance_before_wheel = camera.state().distance;
  const quader::QVec3 target_before_wheel = camera.state().target;
  QWheelEvent wheel_out(panned, panned_global, {}, QPoint{0, -120},
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                        false);
  QApplication::sendEvent(surface, &wheel_out);
  process_input();
  EXPECT_NEAR(camera.state().distance, distance_before_wheel * 1.18f,
              0.001f);
  QWheelEvent wheel_back_in(panned, panned_global, {}, QPoint{0, 120},
                            Qt::NoButton, Qt::NoModifier,
                            Qt::NoScrollPhase, false);
  QApplication::sendEvent(surface, &wheel_back_in);
  process_input();
  EXPECT_NEAR(camera.state().distance, distance_before_wheel, 0.001f);
  EXPECT_NEAR(quader::length(camera.state().target - target_before_wheel),
              0.0f, 0.0001f);

  const quader::QVec3 visible_orbit_anchor{0.0f, 0.0f, -4.0f};
  const quader::ViewportScreenPoint visible_anchor_before =
      camera.project_world_point(visible_orbit_anchor);
  ASSERT_TRUE(visible_anchor_before.visible);
  const QPointF orbit_press_local{visible_anchor_before.position.x,
                                  visible_anchor_before.position.y};
  const QPointF orbit_press_global{orbit_press_local.x() + 100.0,
                                   orbit_press_local.y() + 100.0};
  QMouseEvent fresh_orbit_press(
      QEvent::MouseButtonPress, orbit_press_local, orbit_press_global,
      Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &fresh_orbit_press);
  process_input();
  const quader::ViewportCameraPose pan_zoom_pose = camera.pose();
  const QPointF fresh_orbit_moved{orbit_press_local.x() + 5.0,
                                  orbit_press_local.y()};
  const QPointF fresh_orbit_moved_global{orbit_press_global.x() + 5.0,
                                         orbit_press_global.y()};
  QMouseEvent fresh_orbit_move(
      QEvent::MouseMove, fresh_orbit_moved, fresh_orbit_moved_global,
      Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &fresh_orbit_move);
  process_input();
  QMouseEvent fresh_orbit_release(
      QEvent::MouseButtonRelease, fresh_orbit_moved,
      fresh_orbit_moved_global, Qt::MiddleButton, Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(surface, &fresh_orbit_release);
  process_input();
  const quader::ViewportCameraPose pan_zoom_orbit_pose = camera.pose();
  const float fresh_orbit_angle = std::acos(std::clamp(
      quader::dot(
          quader::normalize(pan_zoom_pose.target - pan_zoom_pose.eye),
          quader::normalize(pan_zoom_orbit_pose.target -
                            pan_zoom_orbit_pose.eye)),
      -1.0f, 1.0f));
  const float expected_fresh_orbit_angle =
      5.0f * static_cast<float>(
                 quader::viewport_host_defaults().orbit_radians_per_pixel);
  EXPECT_NEAR(fresh_orbit_angle, expected_fresh_orbit_angle, 0.0005f);
  EXPECT_LE(quader::length(pan_zoom_orbit_pose.eye - pan_zoom_pose.eye),
            2.0f * quader::length(pan_zoom_pose.eye - visible_orbit_anchor) *
                    std::sin(expected_fresh_orbit_angle * 0.5f) +
                0.001f);
  const quader::ViewportScreenPoint visible_anchor_after =
      camera.project_world_point(visible_orbit_anchor);
  ASSERT_TRUE(visible_anchor_after.visible);
  EXPECT_NEAR(visible_anchor_after.position.x,
              visible_anchor_before.position.x, 0.05f);
  EXPECT_NEAR(visible_anchor_after.position.y,
              visible_anchor_before.position.y, 0.05f);

  // A selected object can be in front of the camera yet far outside the
  // viewport. It must not override the render-visible surface under the exact
  // MMB press, or a small drag would orbit around a kilometre-wide radius.
  const auto offscreen = editor.modeling().create_box(
      {.min = {9999.0f, -1.0f, -6.0f},
       .max = {10001.0f, 1.0f, -4.0f}});
  ASSERT_TRUE(offscreen.ok()) << offscreen.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().select_object(offscreen.value()).ok());
  restore_stale_pose();
  const std::optional<quader::QVec3> selected_center =
      editor.query().selection_center();
  ASSERT_TRUE(selected_center.has_value());
  const quader::ViewportScreenPoint selected_projection =
      camera.project_world_point(*selected_center);
  ASSERT_TRUE(selected_projection.visible);
  ASSERT_GT(selected_projection.position.x, 800.0f);
  const quader::ViewportScreenPoint fallback_before =
      camera.project_world_point({0.0f, 0.0f, -4.0f});

  QMouseEvent fallback_press(QEvent::MouseButtonPress, center, center_global,
                             Qt::MiddleButton, Qt::MiddleButton,
                             Qt::NoModifier);
  QApplication::sendEvent(surface, &fallback_press);
  process_input();
  QMouseEvent fallback_move(QEvent::MouseMove, moved, moved_global,
                            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &fallback_move);
  process_input();

  EXPECT_NEAR(camera.state().distance, 4.0f, 0.01f);
  const quader::ViewportScreenPoint fallback_after =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(fallback_after.visible);
  EXPECT_NEAR(fallback_after.position.x, fallback_before.position.x, 0.05f);
  EXPECT_NEAR(fallback_after.position.y, fallback_before.position.y, 0.05f);

  QMouseEvent fallback_release(QEvent::MouseButtonRelease, moved,
                               moved_global, Qt::MiddleButton, Qt::NoButton,
                               Qt::NoModifier);
  QApplication::sendEvent(surface, &fallback_release);
  process_input();

  // A locally isolated selected object can still exist in scene selection.
  // It must not override the actually rendered surface at the MMB press.
  const auto locally_hidden = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -102.0f},
       .max = {1.0f, 1.0f, -100.0f}});
  ASSERT_TRUE(locally_hidden.ok()) << locally_hidden.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().select_object(locally_hidden.value()).ok());
  const std::array<quader::ViewportLocalVisibilityEntityId, 3u> baseline{
      box.value().entity, offscreen.value().entity,
      locally_hidden.value().entity};
  const std::array<quader::ViewportLocalVisibilityEntityId, 1u> members{
      box.value().entity};
  ASSERT_TRUE(session.viewport_workspace().enter_local_visibility(baseline,
                                                                  members));
  restore_stale_pose();
  const std::optional<quader::QVec3> locally_hidden_center =
      editor.query().selection_center();
  ASSERT_TRUE(locally_hidden_center.has_value());
  const quader::ViewportScreenPoint hidden_projection =
      camera.project_world_point(*locally_hidden_center);
  ASSERT_TRUE(hidden_projection.visible);
  EXPECT_NEAR(hidden_projection.position.x, center.x(), 0.01f);
  EXPECT_NEAR(hidden_projection.position.y, center.y(), 0.01f);
  const quader::ViewportScreenPoint visible_before =
      camera.project_world_point({0.0f, 0.0f, -4.0f});

  QMouseEvent isolated_press(QEvent::MouseButtonPress, center, center_global,
                             Qt::MiddleButton, Qt::MiddleButton,
                             Qt::NoModifier);
  QApplication::sendEvent(surface, &isolated_press);
  process_input();
  QMouseEvent isolated_move(QEvent::MouseMove, moved, moved_global,
                            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &isolated_move);
  process_input();

  EXPECT_NEAR(camera.state().distance, 4.0f, 0.01f);
  const quader::ViewportScreenPoint visible_after =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(visible_after.visible);
  EXPECT_NEAR(visible_after.position.x, visible_before.position.x, 0.05f);
  EXPECT_NEAR(visible_after.position.y, visible_before.position.y, 0.05f);

  QMouseEvent isolated_release(QEvent::MouseButtonRelease, moved, moved_global,
                               Qt::MiddleButton, Qt::NoButton,
                               Qt::NoModifier);
  QApplication::sendEvent(surface, &isolated_release);
  process_input();
  ASSERT_TRUE(session.viewport_workspace().exit_local_visibility());
  process_input();

  // The selected object is now visible again but sits far behind the box
  // under MMB. The pressed render surface must win over that occluded,
  // on-screen selection or the orbit radius would jump from four to 101.
  ASSERT_TRUE(camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 0.0f},
      .target = {0.0f, 0.0f, -4.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const std::optional<quader::QVec3> occluded_selected_center =
      editor.query().selection_center();
  ASSERT_TRUE(occluded_selected_center.has_value());
  const quader::ViewportScreenPoint occluded_projection =
      camera.project_world_point(*occluded_selected_center);
  ASSERT_TRUE(occluded_projection.visible);
  EXPECT_NEAR(occluded_projection.position.x, center.x(), 0.01f);
  EXPECT_NEAR(occluded_projection.position.y, center.y(), 0.01f);
  const quader::ViewportScreenPoint pressed_surface_before =
      camera.project_world_point({0.0f, 0.0f, -4.0f});

  QMouseEvent occluded_press(QEvent::MouseButtonPress, center, center_global,
                             Qt::MiddleButton, Qt::MiddleButton,
                             Qt::NoModifier);
  QApplication::sendEvent(surface, &occluded_press);
  process_input();
  QMouseEvent occluded_move(QEvent::MouseMove, moved, moved_global,
                            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &occluded_move);
  process_input();

  EXPECT_NEAR(camera.state().distance, 4.0f, 0.01f);
  const quader::ViewportScreenPoint pressed_surface_after =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(pressed_surface_after.visible);
  EXPECT_NEAR(pressed_surface_after.position.x,
              pressed_surface_before.position.x, 0.05f);
  EXPECT_NEAR(pressed_surface_after.position.y,
              pressed_surface_before.position.y, 0.05f);

  QMouseEvent occluded_release(QEvent::MouseButtonRelease, moved,
                               moved_global, Qt::MiddleButton, Qt::NoButton,
                               Qt::NoModifier);
  QApplication::sendEvent(surface, &occluded_release);
  process_input();

  // An on-screen selection is not an unconditional global pivot. When MMB is
  // pressed far away on empty background, keep the valid view-center radius
  // instead of jumping out to that distant selected object.
  ASSERT_TRUE(camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 0.0f},
      .target = {0.0f, 0.0f, -4.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const QPointF far_selection_blank{20.0, 20.0};
  const QPointF far_selection_blank_global{120.0, 120.0};
  QMouseEvent far_selection_press(
      QEvent::MouseButtonPress, far_selection_blank,
      far_selection_blank_global, Qt::MiddleButton, Qt::MiddleButton,
      Qt::NoModifier);
  QApplication::sendEvent(surface, &far_selection_press);
  process_input();
  const QPointF far_selection_moved{25.0, 20.0};
  const QPointF far_selection_moved_global{125.0, 120.0};
  QMouseEvent far_selection_move(
      QEvent::MouseMove, far_selection_moved,
      far_selection_moved_global, Qt::NoButton, Qt::MiddleButton,
      Qt::NoModifier);
  QApplication::sendEvent(surface, &far_selection_move);
  process_input();
  EXPECT_NEAR(camera.state().distance, 4.0f, 0.0001f);
  QMouseEvent far_selection_release(
      QEvent::MouseButtonRelease, far_selection_moved,
      far_selection_moved_global, Qt::MiddleButton, Qt::NoButton,
      Qt::NoModifier);
  QApplication::sendEvent(surface, &far_selection_release);
  process_input();

  // A selected child whose own visibility flag is enabled is still absent
  // from the viewport when one of its parents is hidden. Effective hierarchy
  // visibility must therefore reject its center as an orbit anchor too.
  const quader::EntityId hidden_parent =
      session.scene().create_empty("Hidden navigation parent");
  const auto hidden_child = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -102.0f},
       .max = {1.0f, 1.0f, -100.0f}});
  ASSERT_TRUE(hidden_child.ok()) << hidden_child.error_message();
  ASSERT_TRUE(session.scene().set_parent(hidden_child.value().entity,
                                         hidden_parent, false));
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.selection().select_object(hidden_child.value()).ok());
  ASSERT_TRUE(session.scene().set_visible(hidden_parent, false));
  ASSERT_TRUE(session.scene().visibility(hidden_child.value().entity)->visible);
  ASSERT_FALSE(
      session.scene().is_effectively_visible(hidden_child.value().entity));
  restore_stale_pose();
  const std::optional<quader::QVec3> hidden_child_center =
      editor.query().selection_center();
  ASSERT_FALSE(hidden_child_center.has_value());
  const quader::ViewportScreenPoint hidden_child_projection =
      camera.project_world_point({0.0f, 0.0f, -101.0f});
  ASSERT_TRUE(hidden_child_projection.visible);
  EXPECT_NEAR(hidden_child_projection.position.x, center.x(), 0.01f);
  EXPECT_NEAR(hidden_child_projection.position.y, center.y(), 0.01f);
  const quader::ViewportScreenPoint hierarchy_visible_before =
      camera.project_world_point({0.0f, 0.0f, -4.0f});

  QMouseEvent hierarchy_press(QEvent::MouseButtonPress, center, center_global,
                              Qt::MiddleButton, Qt::MiddleButton,
                              Qt::NoModifier);
  QApplication::sendEvent(surface, &hierarchy_press);
  process_input();
  QMouseEvent hierarchy_move(QEvent::MouseMove, moved, moved_global,
                             Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &hierarchy_move);
  process_input();

  EXPECT_NEAR(camera.state().distance, 4.0f, 0.01f);
  const quader::ViewportScreenPoint hierarchy_visible_after =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(hierarchy_visible_after.visible);
  EXPECT_NEAR(hierarchy_visible_after.position.x,
              hierarchy_visible_before.position.x, 0.05f);
  EXPECT_NEAR(hierarchy_visible_after.position.y,
              hierarchy_visible_before.position.y, 0.05f);

  QMouseEvent hierarchy_release(QEvent::MouseButtonRelease, moved,
                                moved_global, Qt::MiddleButton, Qt::NoButton,
                                Qt::NoModifier);
  QApplication::sendEvent(surface, &hierarchy_release);
  process_input();

  // Empty-background orbit falls back to whole-scene depth. That fallback may
  // repair a poisoned far target, but distant bounds must never inflate an
  // already valid nearby orbit radius.
  ASSERT_TRUE(camera.restore_perspective_pose({
      .eye = {0.0f, 0.0f, 0.0f},
      .target = {0.0f, 0.0f, -5.0f},
      .up = {0.0f, 1.0f, 0.0f},
  }));
  const quader::ViewportCameraPose blank_before = camera.pose();
  const QPointF blank{20.0, 20.0};
  const QPointF blank_global{120.0, 120.0};
  QMouseEvent blank_press(QEvent::MouseButtonPress, blank, blank_global,
                          Qt::MiddleButton, Qt::MiddleButton,
                          Qt::NoModifier);
  QApplication::sendEvent(surface, &blank_press);
  process_input();
  EXPECT_EQ(quader::viewport_camera_pose_persistence_signature(camera.pose()),
            quader::viewport_camera_pose_persistence_signature(blank_before));

  const QPointF blank_moved{25.0, 20.0};
  const QPointF blank_moved_global{125.0, 120.0};
  QMouseEvent blank_move(QEvent::MouseMove, blank_moved, blank_moved_global,
                         Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &blank_move);
  process_input();

  EXPECT_NEAR(camera.state().distance, 5.0f, 0.0001f);
  EXPECT_NEAR(
      std::acos(std::clamp(
          quader::dot(
              quader::normalize(blank_before.target - blank_before.eye),
              quader::normalize(camera.pose().target - camera.pose().eye)),
          -1.0f, 1.0f)),
      5.0f * static_cast<float>(
                 quader::viewport_host_defaults().orbit_radians_per_pixel),
      0.0005f);

  QMouseEvent blank_release(QEvent::MouseButtonRelease, blank_moved,
                            blank_moved_global, Qt::MiddleButton,
                            Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &blank_release);
  process_input();
}

TEST(QtViewportSurfaceInputTests,
     AbandonInputDiscardsQueuedHeldAndTrailingReleaseEdges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-abandon-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  const QPointF local{24.0, 32.0};
  const QPointF global{124.0, 232.0};
  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(&surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(&surface, &focus);
  QKeyEvent key_press(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier,
                      QStringLiteral("a"));
  QApplication::sendEvent(&surface, &key_press);
  QMouseEvent mouse_press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&surface, &mouse_press);
  QWheelEvent wheel(local, global, {}, QPoint{0, 120}, Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&surface, &wheel);

  surface.abandon_input();

  QKeyEvent key_release(QEvent::KeyRelease, Qt::Key_A, Qt::ControlModifier,
                        QStringLiteral("a"));
  QApplication::sendEvent(&surface, &key_release);
  QMouseEvent mouse_release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&surface, &mouse_release);
  const quader::QtViewportInputFrame abandoned =
      surface.take_input_frame(1.0f / 60.0f, false);

  EXPECT_TRUE(abandoned.input.viewport_hovered);
  EXPECT_TRUE(abandoned.input.viewport_focused);
  EXPECT_TRUE(abandoned.input.pressed_input_chords.empty());
  EXPECT_TRUE(abandoned.input.released_input_chords.empty());
  EXPECT_TRUE(abandoned.input.active_input_chords.empty());
  EXPECT_TRUE(abandoned.input.pointer_pressed_edges.empty());
  EXPECT_TRUE(abandoned.input.pointer_released_edges.empty());
  EXPECT_TRUE(abandoned.input.pointer_drag_started_edges.empty());
  EXPECT_TRUE(abandoned.input.pointer_motion_samples.empty());
  EXPECT_TRUE(abandoned.input.wheel_input_edges.empty());
  EXPECT_FALSE(abandoned.left_pressed);
  EXPECT_FALSE(abandoned.left_down);
  EXPECT_FALSE(abandoned.left_released);
  EXPECT_FALSE(abandoned.pointer_activity());

  QKeyEvent fresh_press(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier,
                        QStringLiteral("b"));
  QApplication::sendEvent(&surface, &fresh_press);
  QKeyEvent fresh_release(QEvent::KeyRelease, Qt::Key_B, Qt::NoModifier,
                          QStringLiteral("b"));
  QApplication::sendEvent(&surface, &fresh_release);
  const quader::QtViewportInputFrame fresh =
      surface.take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(fresh.input.pressed_input_chords.size(), 1u);
  ASSERT_EQ(fresh.input.released_input_chords.size(), 1u);
  EXPECT_EQ(fresh.input.pressed_input_chords.front(),
            fresh.input.released_input_chords.front());
}

TEST(QtViewportSurfaceInputTests,
     NavigationOwnerIsCanceledBeforePaneOrWorkspaceChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-navigation-owner-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession first_session;
  quader::SceneSession second_session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(
          first_session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(first_session.viewport_workspace().set_layout(
      quader::ViewportLayoutId::FourQuad));

  quader::QtViewportPanel panel;
  panel.attach_workspace(first_session.viewport_workspace());
  quader::QtViewportSurface *top_surface =
      panel.surface(quader::ViewportPaneId::Top);
  ASSERT_NE(top_surface, nullptr);
  top_surface->resize(800, 600);
  const QPointF center{400.0, 300.0};
  const QPointF center_global{500.0, 400.0};
  QEnterEvent top_enter(center, center, center_global);
  QApplication::sendEvent(top_surface, &top_enter);
  QFocusEvent top_focus(QEvent::FocusIn);
  QApplication::sendEvent(top_surface, &top_focus);

  const auto process_first = [&]() {
    panel.process_input(
        editor, first_session.scene(), first_session.assets(),
        first_session.modeling(), {}, first_session.native_tools(), nullptr,
        1.0 / 60.0);
  };
  process_first();
  QMouseEvent top_press(QEvent::MouseButtonPress, center, center_global,
                        Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(top_surface, &top_press);
  process_first();
  ASSERT_TRUE(first_session.viewport_workspace()
                  .camera(quader::ViewportPaneId::Top)
                  .is_dragging());

  ASSERT_TRUE(first_session.viewport_workspace().set_layout(
      quader::ViewportLayoutId::Single));
  panel.refresh_workspace_layout();
  EXPECT_FALSE(first_session.viewport_workspace()
                   .camera(quader::ViewportPaneId::Top)
                   .is_dragging());

  quader::QtViewportSurface *perspective_surface =
      panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(perspective_surface, nullptr);
  perspective_surface->resize(800, 600);
  QEnterEvent perspective_enter(center, center, center_global);
  QApplication::sendEvent(perspective_surface, &perspective_enter);
  QFocusEvent perspective_focus(QEvent::FocusIn);
  QApplication::sendEvent(perspective_surface, &perspective_focus);
  process_first();
  QMouseEvent perspective_press(
      QEvent::MouseButtonPress, center, center_global, Qt::MiddleButton,
      Qt::MiddleButton, Qt::NoModifier);
  QApplication::sendEvent(perspective_surface, &perspective_press);
  process_first();
  ASSERT_TRUE(first_session.viewport_workspace()
                  .camera(quader::ViewportPaneId::Perspective)
                  .is_dragging());

  panel.attach_workspace(second_session.viewport_workspace());
  EXPECT_FALSE(first_session.viewport_workspace()
                   .camera(quader::ViewportPaneId::Perspective)
                   .is_dragging());
  EXPECT_FALSE(second_session.viewport_workspace()
                   .camera(quader::ViewportPaneId::Perspective)
                   .is_dragging());
}

TEST(QtViewportSurfaceInputTests,
     CloseSceneShortcutStopsTheOldInputFrameAfterRuntimeRebind) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-close-scene-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  quader::ProjectSession project;
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const quader::QuaderApiVoidResult created = editor.project().create(
      std::filesystem::path(temporary.path().toStdWString()));
  ASSERT_TRUE(created.ok()) << created.error_message();
  const std::optional<quader::SceneSessionId> surviving_scene_id =
      editor.scene_documents().active_scene_id();
  ASSERT_TRUE(surviving_scene_id.has_value());
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef>
      surviving_object = editor.modeling().create_box(
          {.min = {-1.0f, -1.0f, -1.0f},
           .max = {1.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(surviving_object.ok()) << surviving_object.error_message();
  ASSERT_TRUE(editor.selection().clear().ok());
  const quader::QuaderApiResult<quader::SceneSessionId> closing_scene =
      editor.scene_documents().new_scene();
  ASSERT_TRUE(closing_scene.ok()) << closing_scene.error_message();
  ASSERT_NE(closing_scene.value(), *surviving_scene_id);
  ASSERT_NE(project.active_scene(), nullptr);

  quader::QtViewportPanel panel;
  quader::SceneSession *bound_scene = project.active_scene()->session.get();
  ASSERT_NE(bound_scene, nullptr);
  quader::SceneSession *const closing_scene_owner = bound_scene;
  const quader::OpenSceneSession *surviving_scene =
      project.find(*surviving_scene_id);
  ASSERT_NE(surviving_scene, nullptr);
  quader::SceneSession *const surviving_scene_owner =
      surviving_scene->session.get();
  ASSERT_NE(surviving_scene_owner, nullptr);
  int rebind_count = 0;
  quader::QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      editor, [&](quader::SceneSession &scene) {
        ++rebind_count;
        panel.abandon_interactions();
        bound_scene = &scene;
        panel.attach_workspace(scene.viewport_workspace());
      });
  panel.attach_workspace(bound_scene->viewport_workspace());
  quader::QtViewportSurface *surface =
      panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);

  const auto process_bound_scene = [&]() {
    panel.process_input(
        editor, bound_scene->scene(), bound_scene->assets(),
        bound_scene->modeling(), {}, bound_scene->native_tools(),
        &bound_scene->point_placement(), 1.0 / 60.0);
  };
  process_bound_scene();

  QKeyEvent close_press(QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier,
                        QStringLiteral("w"));
  QApplication::sendEvent(surface, &close_press);
  QKeyEvent select_all_press(QEvent::KeyPress, Qt::Key_A,
                             Qt::ControlModifier, QStringLiteral("a"));
  QApplication::sendEvent(surface, &select_all_press);
  // Capture the outgoing references exactly as App/UISystem do. The Ctrl+W
  // dispatch destroys this SceneSession and synchronously installs the next
  // one before process_input returns.
  panel.process_input(
      editor, closing_scene_owner->scene(), closing_scene_owner->assets(),
      closing_scene_owner->modeling(), {}, closing_scene_owner->native_tools(),
      &closing_scene_owner->point_placement(), 1.0 / 60.0);

  EXPECT_EQ(rebind_count, 1);
  EXPECT_EQ(bound_scene, surviving_scene_owner);
  EXPECT_EQ(editor.scene_documents().active_scene_id(), surviving_scene_id);
  EXPECT_EQ(project.find(closing_scene.value()), nullptr);
  EXPECT_TRUE(surviving_scene_owner->scene().selected_entities().empty())
      << "the later Ctrl+A edge from the old frame reached the replacement";

  QKeyEvent close_release(QEvent::KeyRelease, Qt::Key_W,
                          Qt::ControlModifier, QStringLiteral("w"));
  QApplication::sendEvent(surface, &close_release);
  QKeyEvent select_all_release(QEvent::KeyRelease, Qt::Key_A,
                               Qt::ControlModifier, QStringLiteral("a"));
  QApplication::sendEvent(surface, &select_all_release);
  process_bound_scene();
  EXPECT_EQ(rebind_count, 1);
  EXPECT_EQ(project.active_scene_id(), *surviving_scene_id);
  EXPECT_TRUE(surviving_scene_owner->scene().selected_entities().empty());
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

  EXPECT_EQ(translation.delta_source, QtWheelDeltaSource::Angle);
  EXPECT_EQ(translation.source_delta, 120);
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
  EXPECT_EQ(smooth_translation.delta_source, QtWheelDeltaSource::Pixel);
  EXPECT_EQ(smooth_translation.source_delta, -9);
  EXPECT_FLOAT_EQ(smooth_translation.mouse_wheel, -0.225f);
  ASSERT_EQ(smooth_translation.wheel_input_edges.size(), 2u);
  EXPECT_EQ(smooth_translation.wheel_input_edges[1].chord.gesture,
            QuaderInputGesture::WheelDown);
  EXPECT_DOUBLE_EQ(smooth_translation.wheel_input_edges[0].step_delta,
                   -0.225);
  EXPECT_DOUBLE_EQ(smooth_translation.wheel_input_edges[1].step_delta,
                   -0.225);

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

TEST(QtInputAdapterTests,
     StatefulAngleWheelRetainsPartialDetentsAndResetsOnReversal) {
  QtWheelInputAccumulator accumulator;
  const QWheelEvent first_half =
      wheel_event({}, QPoint{0, 60}, Qt::ShiftModifier);
  const quader::QtWheelInputTranslation first =
      quader_wheel_input_from_qt_event(first_half, accumulator);
  EXPECT_EQ(first.delta_source, QtWheelDeltaSource::Angle);
  EXPECT_EQ(first.source_delta, 60);
  EXPECT_FLOAT_EQ(first.mouse_wheel, 0.0f);
  EXPECT_TRUE(first.wheel_input_edges.empty());
  EXPECT_EQ(accumulator.angle_delta_remainder, 60);

  const QWheelEvent second_half =
      wheel_event({}, QPoint{0, 60}, Qt::ControlModifier);
  const quader::QtWheelInputTranslation complete =
      quader_wheel_input_from_qt_event(second_half, accumulator);
  EXPECT_FLOAT_EQ(complete.mouse_wheel, 1.0f);
  ASSERT_EQ(complete.wheel_input_edges.size(), 2u);
  EXPECT_TRUE(complete.wheel_input_edges[0].chord.primary);
  EXPECT_TRUE(complete.wheel_input_edges[1].chord.primary);
  EXPECT_FALSE(complete.wheel_input_edges[0].chord.shift);
  EXPECT_DOUBLE_EQ(complete.wheel_input_edges[0].step_delta, 1.0);
  EXPECT_DOUBLE_EQ(complete.wheel_input_edges[1].step_delta, 1.0);
  EXPECT_EQ(accumulator.angle_delta_remainder, 0);

  const quader::QtWheelInputTranslation pending =
      quader_wheel_input_from_qt_event(first_half, accumulator);
  EXPECT_TRUE(pending.wheel_input_edges.empty());
  EXPECT_EQ(accumulator.angle_delta_remainder, 60);

  const QWheelEvent reverse = wheel_event({}, QPoint{0, -120});
  const quader::QtWheelInputTranslation reversed =
      quader_wheel_input_from_qt_event(reverse, accumulator);
  EXPECT_FLOAT_EQ(reversed.mouse_wheel, -1.0f);
  ASSERT_EQ(reversed.wheel_input_edges.size(), 2u);
  EXPECT_EQ(reversed.wheel_input_edges[1].chord.gesture,
            QuaderInputGesture::WheelDown);
  EXPECT_DOUBLE_EQ(reversed.wheel_input_edges[0].step_delta, -1.0);
  EXPECT_DOUBLE_EQ(reversed.wheel_input_edges[1].step_delta, -1.0);
  EXPECT_EQ(accumulator.angle_delta_remainder, 0);
}

TEST(QtInputAdapterTests,
     StatefulAngleWheelEmitsMultipleOrderedIntegralNotches) {
  QtWheelInputAccumulator accumulator;
  const QWheelEvent event =
      wheel_event({}, QPoint{0, 300}, Qt::AltModifier);
  const quader::QtWheelInputTranslation translation =
      quader_wheel_input_from_qt_event(event, accumulator);

  EXPECT_FLOAT_EQ(translation.mouse_wheel, 2.0f);
  ASSERT_EQ(translation.wheel_input_edges.size(), 4u);
  for (std::size_t index = 0; index < 4u; ++index) {
    EXPECT_TRUE(translation.wheel_input_edges[index].chord.alt);
    EXPECT_DOUBLE_EQ(translation.wheel_input_edges[index].step_delta, 1.0);
    EXPECT_EQ(translation.wheel_input_edges[index].chord.gesture,
              index % 2u == 0u ? QuaderInputGesture::Wheel
                               : QuaderInputGesture::WheelUp);
  }
  EXPECT_EQ(accumulator.angle_delta_remainder, 60);
}

TEST(QtInputAdapterTests,
     StatefulPixelWheelUsesIndependentContinuousPixelCalibration) {
  QtWheelInputAccumulator accumulator{
      .angle_delta_remainder = 60,
      .angle_delta_axis = QtWheelInputAccumulator::Axis::Vertical,
  };
  const QWheelEvent event =
      wheel_event(QPoint{0, -10}, {}, Qt::ShiftModifier);
  const quader::QtWheelInputTranslation translation =
      quader_wheel_input_from_qt_event(event, accumulator);

  EXPECT_EQ(translation.delta_source, QtWheelDeltaSource::Pixel);
  EXPECT_EQ(translation.source_delta, -10);
  EXPECT_FLOAT_EQ(translation.mouse_wheel, -0.25f);
  ASSERT_EQ(translation.wheel_input_edges.size(), 2u);
  EXPECT_TRUE(translation.wheel_input_edges[0].chord.shift);
  EXPECT_TRUE(translation.wheel_input_edges[1].chord.shift);
  EXPECT_DOUBLE_EQ(translation.wheel_input_edges[0].step_delta, -0.25);
  EXPECT_DOUBLE_EQ(translation.wheel_input_edges[1].step_delta, -0.25);
  EXPECT_EQ(accumulator.angle_delta_remainder, 0);
  EXPECT_EQ(accumulator.angle_delta_axis,
            QtWheelInputAccumulator::Axis::None);

  const QWheelEvent precise =
      wheel_event(QPoint{0, 10}, QPoint{0, 120});
  const quader::QtWheelInputTranslation preferred_pixel =
      quader_wheel_input_from_qt_event(precise, accumulator);
  EXPECT_EQ(preferred_pixel.delta_source, QtWheelDeltaSource::Pixel);
  EXPECT_EQ(preferred_pixel.source_delta, 10);
  EXPECT_FLOAT_EQ(preferred_pixel.mouse_wheel, 0.25f);
}

TEST(QtViewportSurfaceInputTests,
     CanonicalizesAngleWheelAcrossFramesAndKeepsPixelMotionContinuous) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-wheel-canonicalization-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QWheelEvent half_up = wheel_event({}, QPoint{0, 60});
  QApplication::sendEvent(&surface, &half_up);
  const quader::QtViewportInputFrame pending =
      surface.take_input_frame(0.0f, false);
  EXPECT_FLOAT_EQ(pending.input.mouse_wheel, 0.0f);
  EXPECT_TRUE(pending.input.wheel_input_edges.empty());

  QWheelEvent reverse =
      wheel_event({}, QPoint{0, -120}, Qt::ControlModifier);
  QApplication::sendEvent(&surface, &reverse);
  const quader::QtViewportInputFrame reversed =
      surface.take_input_frame(0.0f, false);
  EXPECT_FLOAT_EQ(reversed.input.mouse_wheel, -1.0f);
  ASSERT_EQ(reversed.input.wheel_input_edges.size(), 2u);
  EXPECT_TRUE(reversed.input.wheel_input_edges[0].chord.primary);
  EXPECT_DOUBLE_EQ(reversed.input.wheel_input_edges[0].step_delta, -1.0);

  QWheelEvent multiple = wheel_event({}, QPoint{0, 240});
  QApplication::sendEvent(&surface, &multiple);
  const quader::QtViewportInputFrame multiple_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_FLOAT_EQ(multiple_frame.input.mouse_wheel, 2.0f);
  ASSERT_EQ(multiple_frame.input.wheel_input_edges.size(), 4u);
  for (const quader::QuaderWheelInputEdge &edge :
       multiple_frame.input.wheel_input_edges) {
    EXPECT_DOUBLE_EQ(edge.step_delta, 1.0);
  }

  QWheelEvent pixels = wheel_event(QPoint{0, -10}, {});
  QApplication::sendEvent(&surface, &pixels);
  const quader::QtViewportInputFrame pixel_frame =
      surface.take_input_frame(0.0f, false);
  EXPECT_FLOAT_EQ(pixel_frame.input.mouse_wheel, -0.25f);
  ASSERT_EQ(pixel_frame.input.wheel_input_edges.size(), 2u);
  EXPECT_DOUBLE_EQ(pixel_frame.input.wheel_input_edges[0].step_delta, -0.25);
  EXPECT_DOUBLE_EQ(pixel_frame.input.wheel_input_edges[1].step_delta, -0.25);

  QWheelEvent pending_before_abandon = wheel_event({}, QPoint{0, 60});
  QApplication::sendEvent(&surface, &pending_before_abandon);
  EXPECT_TRUE(surface.take_input_frame(0.0f, false)
                  .input.wheel_input_edges.empty());
  surface.abandon_input();
  QWheelEvent after_abandon = wheel_event({}, QPoint{0, 60});
  QApplication::sendEvent(&surface, &after_abandon);
  EXPECT_TRUE(surface.take_input_frame(0.0f, false)
                  .input.wheel_input_edges.empty());

  QFocusEvent focus_out(QEvent::FocusOut);
  QApplication::sendEvent(&surface, &focus_out);
  QWheelEvent after_focus_loss = wheel_event({}, QPoint{0, 60});
  QApplication::sendEvent(&surface, &after_focus_loss);
  const quader::QtViewportInputFrame focus_reset =
      surface.take_input_frame(0.0f, false);
  EXPECT_FLOAT_EQ(focus_reset.input.mouse_wheel, 0.0f);
  EXPECT_TRUE(focus_reset.input.wheel_input_edges.empty());
}

TEST(QtViewportSurfaceInputTests,
     CoalescedZoomToCursorPacketsRetainTheirEventTimeLocalPositions) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-wheel-position-provenance-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());

  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  surface.resize(800, 600);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(&surface, &focus);

  const QPointF first_local{80.0, 120.0};
  const QPointF second_local{620.0, 430.0};
  QWheelEvent first(
      first_local, QPointF{180.0, 220.0}, {}, QPoint{0, 120},
      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
  QWheelEvent second(
      second_local, QPointF{720.0, 530.0}, {}, QPoint{0, -120},
      Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&surface, &first);
  QApplication::sendEvent(&surface, &second);

  quader::QtViewportInputFrame frame =
      surface.take_input_frame(0.0f, false);
  ASSERT_EQ(frame.input.wheel_input_edges.size(), 4u);
  EXPECT_TRUE(frame.input.wheel_input_edges[0].has_local_position);
  EXPECT_TRUE(frame.input.wheel_input_edges[1].has_local_position);
  EXPECT_TRUE(frame.input.wheel_input_edges[2].has_local_position);
  EXPECT_TRUE(frame.input.wheel_input_edges[3].has_local_position);
  EXPECT_FLOAT_EQ(frame.input.wheel_input_edges[0].local.x,
                  frame.input.wheel_input_edges[1].local.x);
  EXPECT_FLOAT_EQ(frame.input.wheel_input_edges[0].local.y,
                  frame.input.wheel_input_edges[1].local.y);
  EXPECT_FLOAT_EQ(frame.input.wheel_input_edges[2].local.x,
                  frame.input.wheel_input_edges[3].local.x);
  EXPECT_FLOAT_EQ(frame.input.wheel_input_edges[2].local.y,
                  frame.input.wheel_input_edges[3].local.y);
  EXPECT_NE(frame.input.wheel_input_edges[0].local.x,
            frame.input.wheel_input_edges[2].local.x);
  EXPECT_NE(frame.input.wheel_input_edges[0].local.y,
            frame.input.wheel_input_edges[2].local.y);

  std::vector<QuaderInputChord> suppressed;
  double linear_array_wheel_remainder = 0.0;
  double radial_array_wheel_remainder = 0.0;
  quader::ViewportKeybindingResolver::resolve(
      editor, quader::ViewportToolState{}, frame.input, suppressed,
      linear_array_wheel_remainder, radial_array_wheel_remainder);

  const auto &wheel_events = frame.input.navigation_bindings.wheel_events;
  ASSERT_EQ(wheel_events.size(), 2u);
  EXPECT_EQ(wheel_events[0].action,
            quader::ViewportInputActionKind::ZoomToCursor);
  EXPECT_EQ(wheel_events[1].action,
            quader::ViewportInputActionKind::ZoomToCursor);
  const float scale = static_cast<float>(surface.devicePixelRatio());
  EXPECT_FLOAT_EQ(wheel_events[0].local.x,
                  static_cast<float>(first_local.x()) * scale);
  EXPECT_FLOAT_EQ(wheel_events[0].local.y,
                  static_cast<float>(first_local.y()) * scale);
  EXPECT_FLOAT_EQ(wheel_events[1].local.x,
                  static_cast<float>(second_local.x()) * scale);
  EXPECT_FLOAT_EQ(wheel_events[1].local.y,
                  static_cast<float>(second_local.y()) * scale);
  EXPECT_FLOAT_EQ(wheel_events[0].steps, 1.0f);
  EXPECT_FLOAT_EQ(wheel_events[1].steps, -1.0f);
}

TEST(QtViewportSurfaceInputTests,
     OppositeWheelPacketsRemainOrderedThroughProductionPanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-ordered-wheel-panel-tests";
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
  surface->resize(800, 600);
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);

  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), nullptr, 1.0 / 60.0);
  };
  process_input();

  quader::ViewportCameraController &camera =
      session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  camera.focus_origin(static_cast<float>(
      quader_editor_core::kNavigationMaxCameraDistance));
  QWheelEvent blocked_out(local, global, {}, QPoint{0, -120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
  QWheelEvent reverse_in(local, global, {}, QPoint{0, 120}, Qt::NoButton,
                         Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(surface, &blocked_out);
  QApplication::sendEvent(surface, &reverse_in);
  process_input();
  EXPECT_NEAR(
      camera.state().distance,
      static_cast<float>(quader_editor_core::kNavigationMaxCameraDistance /
                         quader_editor_core::kNavigationZoomStepFactor),
      0.001f);

  camera.focus_origin(static_cast<float>(
      quader_editor_core::kNavigationMinCameraDistance));
  QWheelEvent blocked_in(local, global, {}, QPoint{0, 120}, Qt::NoButton,
                         Qt::NoModifier, Qt::NoScrollPhase, false);
  QWheelEvent reverse_out(local, global, {}, QPoint{0, -120}, Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(surface, &blocked_in);
  QApplication::sendEvent(surface, &reverse_out);
  process_input();
  EXPECT_NEAR(
      camera.state().distance,
      static_cast<float>(quader_editor_core::kNavigationMinCameraDistance *
                         quader_editor_core::kNavigationZoomStepFactor),
      0.00001f);
}

TEST(QtViewportSurfaceInputTests,
     PrimitiveWheelUsesEventPositionsConsumesLimitsAndPreservesKeybindingOwners) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-primitive-wheel-panel-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  quader::ModelingBoxPreview preview;
  preview.ok = preview.valid = true;
  preview.drag_state = quader::ModelingBoxDragState::Height;
  preview.corners = {quader::QVec3{-1, -1, -1}, quader::QVec3{-1, -1, 1},
                     quader::QVec3{1, -1, 1}, quader::QVec3{1, -1, -1},
                     quader::QVec3{-1, 1, -1}, quader::QVec3{-1, 1, 1},
                     quader::QVec3{1, 1, 1}, quader::QVec3{1, 1, -1}};
  preview.corner_count = 8;
  ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Cube").ok);
  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  surface->resize(800, 600);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(surface, &focus);
  auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  camera.focus_origin(10);
  const auto process = [&] {
    panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  process();
  quader::PrimitiveCreationInteraction projection;
  const auto button = [&] {
    const auto pixels = projection.project(
        quader::ViewportPrimitiveCreationApiAdapter::from_api(editor.tools().live_primitive_creation()),
        camera, panel.viewport_raster_scale(quader::ViewportPaneId::Perspective))
                            .controls[6].screen_position;
    return QPointF{pixels.x / surface->devicePixelRatio(), pixels.y / surface->devicePixelRatio()};
  };
  const auto wheel = [&](int delta, bool pixel = false,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const auto point = button();
    QWheelEvent event(point, point, pixel ? QPoint{0, delta} : QPoint{},
                      pixel ? QPoint{} : QPoint{0, delta}, Qt::NoButton,
                      modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(surface, &event);
  };
  const auto count = [&] { return editor.tools().live_primitive_creation()->subdivisions[0]; };
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kBoxSubdivisionsXSetting, 128.0).ok());
  const auto distance = camera.state().distance;
  const auto target = camera.state().target;
  wheel(120);
  wheel(-120);
  // A later mouse packet must not replace the two wheel hit-test positions.
  QMouseEvent move(QEvent::MouseMove, {12, 12}, {12, 12},
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(surface, &move);
  process();
  EXPECT_EQ(count(), 127);
  EXPECT_FLOAT_EQ(camera.state().distance, distance);
  EXPECT_FLOAT_EQ(quader::length_squared(camera.state().target - target), 0.0f);

  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kBoxSubdivisionsXSetting, 128.0).ok());
  wheel(20, true, Qt::ControlModifier); // Blocked half-step still owns Ctrl+zoom.
  process();
  wheel(-20, true, Qt::ControlModifier);
  process();
  EXPECT_EQ(count(), 128);
  EXPECT_FLOAT_EQ(camera.state().distance, distance);
  wheel(-20, true, Qt::ControlModifier);
  process();
  EXPECT_EQ(count(), 127);
  EXPECT_FLOAT_EQ(camera.state().distance, distance);
  EXPECT_FLOAT_EQ(quader::length_squared(camera.state().target - target), 0.0f);

  constexpr auto kFlip = quader::quader_command::kToolsFlipPrimitiveFaces;
  for (const bool sequence : {false, true}) {
    ASSERT_TRUE(editor.commands().set_shortcut(kFlip, sequence ? "K WheelUp" : "WheelUp").ok());
    const bool before = editor.tools().live_primitive_creation()->flip_faces;
    if (sequence) {
      QKeyEvent prefix(QEvent::KeyPress, Qt::Key_K, Qt::NoModifier);
      QApplication::sendEvent(surface, &prefix);
      process();
      ASSERT_TRUE(editor.commands().has_pending_keybinding_chord());
    }
    wheel(120);
    process();
    EXPECT_NE(editor.tools().live_primitive_creation()->flip_faces, before);
    EXPECT_EQ(count(), 127);
    EXPECT_FLOAT_EQ(camera.state().distance, distance);
    EXPECT_FALSE(editor.commands().has_pending_keybinding_chord());
  }
}

TEST(QtViewportSurfaceInputTests, RoundPrimitiveFootprintUsesNaturalHeightThroughPanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-round-footprint-panel-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  for (auto kind : {quader::PrimitiveCreationKind::Sphere,
                    quader::PrimitiveCreationKind::Torus}) {
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
        quader::kPrimitiveTypeSetting, static_cast<double>(kind)).ok());
    quader::QtViewportPanel panel;
    panel.attach_workspace(session.viewport_workspace());
    auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
    ASSERT_NE(surface, nullptr);
    surface->resize(800, 600);
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(surface, &focus);
    auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
    const auto process = [&] {
      panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                          session.native_tools(), &session.point_placement(), 1.0 / 60.0);
    };
    process();
    QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &press);
    process();
    QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &release);
    process();
    const auto primitive = editor.tools().live_primitive_creation();
    ASSERT_TRUE(primitive);
    const auto extent = primitive->local_max - primitive->local_min;
    const double ratio = kind == quader::PrimitiveCreationKind::Sphere
                             ? 1.0 : primitive->tube_radius_ratio;
    EXPECT_NEAR(extent.y, std::min(extent.x, extent.z) * ratio, 0.0001);
    EXPECT_EQ(session.primitive_creation().shape_count(), 1);
    EXPECT_EQ(session.modeling().box_drag_state(), quader::ModelingBoxDragState::Idle);
  }
}
