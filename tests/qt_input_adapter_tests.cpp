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
#include "editor/array/path_array_tool_service.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/tools/primitive_creation_interaction.hpp"
#include "editor/tools/extrude_handle_layout.hpp"
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/tools/uv_peel_tool_service.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "math/glm_bridge.hpp"
#include "ui/qt_application_cursor.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_input_adapter.hpp"
#include "ui/qt_operation_input_hints.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "support/qt_viewport_input_test_helpers.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_pointer_gesture.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEventLoop>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPoint>
#include <QPointF>
#include <QPixmap>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
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
using quader::quader_input_chord_from_qt_event;
using quader::quader_input_chord_from_qt_key;
using quader::quader_input_format_chord;
using quader::quader_input_parse_chord;
using quader::quader_keyboard_event_key_identity;
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

using quader::test_support::resize_viewport_surface;
using quader::test_support::send_visible_viewport_event;

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

void expect_compiled_mesh_equal(const quader::QuaderMeshAsset &actual,
                                const quader::QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &a = actual.vertices[index];
    const auto &b = expected.vertices[index];
    EXPECT_FLOAT_EQ(a.position.x, b.position.x);
    EXPECT_FLOAT_EQ(a.position.y, b.position.y);
    EXPECT_FLOAT_EQ(a.position.z, b.position.z);
    EXPECT_FLOAT_EQ(a.uv.x, b.uv.x);
    EXPECT_FLOAT_EQ(a.uv.y, b.uv.y);
  }
}

/// Drives navigation through the production Qt panel without a visible window.
struct NavigationPanelTestContext {
  quader::SceneSession session;
  quader::QuaderEditorApi editor{
      quader::QuaderApiSessionInternalAccess::from_scene_session(session)};
  quader::QtViewportPanel panel;
  quader::QtViewportSurface *surface = nullptr;
  quader::ViewportCameraController &camera =
      session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);

  NavigationPanelTestContext() {
    EXPECT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    panel.attach_workspace(session.viewport_workspace());
    surface = panel.surface(quader::ViewportPaneId::Perspective);
    if (surface == nullptr) {
      return;
    }
    resize_viewport_surface(panel, *surface, {800, 600});
    QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &enter);
    send_visible_viewport_event(surface, &focus);
  }

  void process(double delta_seconds = 1.0 / 60.0) {
    panel.process_input(editor, session.scene(), session.assets(),
                        session.modeling(), {}, session.native_tools(),
                        &session.point_placement(), delta_seconds);
  }

  void key(int key) {
    QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
    send_visible_viewport_event(surface, &press);
    send_visible_viewport_event(surface, &release);
    process();
  }

  void wheel(QPointF local, int angle_delta,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QWheelEvent event(local, local + QPointF{100, 100}, {},
                      QPoint{0, angle_delta}, Qt::NoButton, modifiers,
                      Qt::NoScrollPhase, false);
    send_visible_viewport_event(surface, &event);
  }

  void mouse(QEvent::Type type, QPointF local, Qt::MouseButton button,
             Qt::MouseButtons buttons,
             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, local, local + QPointF{100, 100}, button,
                      buttons, modifiers);
    send_visible_viewport_event(surface, &event);
  }
};

/// Detects a transiently empty hint row even if its final contents recover.
class InputHintVisibilityProbe final : public QObject {
public:
  int hides = 0;

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (event->type() == QEvent::Hide) {
      ++hides;
    }
    return QObject::eventFilter(watched, event);
  }
};

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

TEST(QtInputAdapterTests,
     KeyboardEventsWithoutNativeKeysRetainShiftedSymbolFallback) {
  constexpr std::array kMappings{
      KeyMapping{Qt::Key_At, QuaderKeyCode::Digit2},
      KeyMapping{Qt::Key_NumberSign, QuaderKeyCode::Digit3},
      KeyMapping{Qt::Key_QuoteDbl, QuaderKeyCode::Apostrophe},
  };
  for (const KeyMapping &mapping : kMappings) {
    SCOPED_TRACE(mapping.qt_key);
    const QKeyEvent event(QEvent::KeyPress, mapping.qt_key,
                          Qt::ShiftModifier);
    const auto chord = quader_input_chord_from_qt_event(event);
    EXPECT_EQ(chord.key, mapping.quader_key);
    EXPECT_TRUE(chord.shift);
    EXPECT_EQ(event.nativeVirtualKey(), 0u);
  }
  const QKeyEvent unsupported(QEvent::KeyPress, Qt::Key_sterling,
                              Qt::ShiftModifier);
  EXPECT_TRUE(quader_input_chord_from_qt_event(unsupported).empty());
}

TEST(QtInputAdapterTests, AltGrDoesNotDowngradeToAnOrdinaryBinding) {
  const Qt::KeyboardModifiers modifiers =
      Qt::GroupSwitchModifier | Qt::ControlModifier | Qt::AltModifier;
  const QKeyEvent native(QEvent::KeyPress, Qt::Key_E, modifiers,
                         0x12, 0x45, 0, QStringLiteral("e"));
  EXPECT_TRUE(quader_input_chord_from_qt_event(native).empty());
  EXPECT_FALSE(quader_pressed_input_chord_from_qt_event(native).has_value());
  EXPECT_TRUE(quader_input_chord_from_qt_key(Qt::Key_E, modifiers).empty());
  EXPECT_FALSE(quader_key_code_from_qt_key(Qt::Key_E, modifiers).has_value());
  for (const auto type : {QEvent::MouseButtonPress,
                          QEvent::MouseButtonRelease,
                          QEvent::MouseButtonDblClick}) {
    const auto mouse = mouse_event(type, Qt::LeftButton, modifiers);
    for (const auto gesture : {QuaderInputGesture::Press,
                               QuaderInputGesture::Click,
                               QuaderInputGesture::DoubleClick}) {
      EXPECT_FALSE(quader_mouse_input_chord_from_qt_event(mouse, gesture)
                       .has_value());
    }
  }
  const auto wheel = wheel_event({}, {0, 120}, modifiers);
  EXPECT_FALSE(quader_wheel_input_chord_from_qt_event(wheel).has_value());
  EXPECT_TRUE(quader_wheel_input_from_qt_event(wheel).wheel_input_edges.empty());
  QtWheelInputAccumulator accumulator;
  const auto accumulated = quader_wheel_input_from_qt_event(wheel, accumulator);
  EXPECT_TRUE(accumulated.wheel_input_edges.empty());
  EXPECT_EQ(accumulated.mouse_wheel, 0.0f);
  EXPECT_EQ(accumulator.angle_delta_remainder, 0);

  const QKeyEvent ordinary(QEvent::KeyPress, Qt::Key_E,
                           Qt::ControlModifier | Qt::AltModifier);
  const auto chord = quader_input_chord_from_qt_event(ordinary);
  EXPECT_EQ(chord.key, QuaderKeyCode::E);
  EXPECT_TRUE(chord.primary);
  EXPECT_TRUE(chord.alt);
}

TEST(QtInputAdapterTests, KeyboardIdentitySurvivesGlyphAndModifierChanges) {
  const QKeyEvent press(QEvent::KeyPress, Qt::Key_sterling,
                        Qt::ShiftModifier, 0x04, 0x33, 0);
  const QKeyEvent release(QEvent::KeyRelease, Qt::Key_3,
                          Qt::NoModifier, 0x04, 0x33, 0);
  const QKeyEvent layout_change(QEvent::KeyRelease, Qt::Key_NumberSign,
                                Qt::AltModifier, 0x04, 0xbf, 0);
  EXPECT_EQ(quader_keyboard_event_key_identity(press),
            quader_keyboard_event_key_identity(release));
  EXPECT_EQ(quader_keyboard_event_key_identity(press),
            quader_keyboard_event_key_identity(layout_change));
  const QKeyEvent main_enter(QEvent::KeyPress, Qt::Key_Return,
                             Qt::NoModifier, 0x1c, 0x0d, 0);
  const QKeyEvent keypad_enter(QEvent::KeyPress, Qt::Key_Enter,
                               Qt::KeypadModifier, 0xe01c, 0x0d, 0);
  EXPECT_NE(quader_keyboard_event_key_identity(main_enter),
            quader_keyboard_event_key_identity(keypad_enter));
}

TEST(QtInputAdapterTests, KeyboardIdentityKeepsNativeAndQtFallbacksDistinct) {
  const QKeyEvent scan(QEvent::KeyPress, Qt::Key_unknown,
                       Qt::NoModifier, 0x33, 0, 0);
  const QKeyEvent virtual_key(QEvent::KeyPress, Qt::Key_sterling,
                              Qt::ShiftModifier, 0, 0x33, 0);
  const QKeyEvent virtual_release(QEvent::KeyRelease, Qt::Key_3,
                                  Qt::NoModifier, 0, 0x33, 0);
  const QKeyEvent qt_key(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier);
  const QKeyEvent qt_release(QEvent::KeyRelease, Qt::Key_3,
                             Qt::ShiftModifier);
  EXPECT_EQ(quader_keyboard_event_key_identity(virtual_key),
            quader_keyboard_event_key_identity(virtual_release));
  EXPECT_EQ(quader_keyboard_event_key_identity(qt_key),
            quader_keyboard_event_key_identity(qt_release));
  EXPECT_NE(quader_keyboard_event_key_identity(scan),
            quader_keyboard_event_key_identity(virtual_key));
  EXPECT_NE(quader_keyboard_event_key_identity(scan),
            quader_keyboard_event_key_identity(qt_key));
  EXPECT_NE(quader_keyboard_event_key_identity(virtual_key),
            quader_keyboard_event_key_identity(qt_key));
}

TEST(QtInputAdapterTests, InternationalBackslashHasAnIndependentBindingToken) {
  const auto international = quader_input_parse_chord("Ctrl+IntlBackslash");
  const auto backslash = quader_input_parse_chord("Ctrl+Backslash");
  ASSERT_TRUE(international.has_value());
  ASSERT_TRUE(backslash.has_value());
  EXPECT_EQ(international->key, QuaderKeyCode::IntlBackslash);
  EXPECT_NE(international->key, backslash->key);
  const auto formatted = quader_input_format_chord(*international);
  EXPECT_EQ(formatted, "Ctrl+IntlBackslash");
  const auto restored = quader_input_parse_chord(formatted);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->key, international->key);
  EXPECT_EQ(restored->primary, international->primary);
}

#ifdef Q_OS_WIN
TEST(QtInputAdapterTests, WindowsNativeLettersRetainLogicalLayoutIdentity) {
  const QKeyEvent azerty_a(QEvent::KeyPress, Qt::Key_A,
                           Qt::NoModifier, 0x10, 0x41, 0,
                           QStringLiteral("a"));
  const QKeyEvent azerty_q(QEvent::KeyPress, Qt::Key_Q,
                           Qt::NoModifier, 0x1e, 0x51, 0,
                           QStringLiteral("q"));
  EXPECT_EQ(quader_input_chord_from_qt_event(azerty_a).key, QuaderKeyCode::A);
  EXPECT_EQ(quader_input_chord_from_qt_event(azerty_q).key, QuaderKeyCode::Q);
  for (const int glyph : {0x0423, 0x0395}) {
    SCOPED_TRACE(glyph);
    const QKeyEvent event(QEvent::KeyPress, glyph,
                          Qt::ControlModifier | Qt::AltModifier |
                              Qt::ShiftModifier,
                          0x12, 0x45, 0,
                          QString(QChar(static_cast<quint16>(glyph))));
    const auto chord = quader_input_chord_from_qt_event(event);
    EXPECT_EQ(chord.key, QuaderKeyCode::E);
    EXPECT_TRUE(chord.primary);
    EXPECT_TRUE(chord.alt);
    EXPECT_TRUE(chord.shift);
    EXPECT_EQ(event.key(), glyph);
  }
  for (int offset = 0; offset < 26; ++offset) {
    SCOPED_TRACE(offset);
    const QKeyEvent event(QEvent::KeyPress, 0x042f, Qt::NoModifier,
                          0, 0x41 + static_cast<quint32>(offset), 0,
                          QStringLiteral("\u042f"));
    const auto chord = quader_input_chord_from_qt_event(event);
    EXPECT_EQ(chord.key, static_cast<QuaderKeyCode>(
                             static_cast<int>(QuaderKeyCode::A) + offset));
  }
}

TEST(QtInputAdapterTests, WindowsNativeFunctionAndNumpadKeysIgnoreEmittedGlyph) {
  for (int offset = 0; offset < 24; ++offset) {
    SCOPED_TRACE(offset);
    const QKeyEvent event(QEvent::KeyPress, Qt::Key_unknown,
                          Qt::ShiftModifier, 0,
                          0x70 + static_cast<quint32>(offset), 0);
    const auto chord = quader_input_chord_from_qt_event(event);
    EXPECT_EQ(chord.key, static_cast<QuaderKeyCode>(
                             static_cast<int>(QuaderKeyCode::F1) + offset));
    EXPECT_TRUE(chord.shift);
  }
  for (int offset = 0; offset < 10; ++offset) {
    SCOPED_TRACE(offset);
    const QKeyEvent event(QEvent::KeyPress, 0x0660 + offset,
                          Qt::KeypadModifier | Qt::AltModifier, 0,
                          0x60 + static_cast<quint32>(offset), 0);
    const auto chord = quader_input_chord_from_qt_event(event);
    EXPECT_EQ(chord.key, static_cast<QuaderKeyCode>(
                             static_cast<int>(QuaderKeyCode::Numpad0) + offset));
    EXPECT_TRUE(chord.alt);
  }
  for (const auto qt_key : {Qt::Key_Enter, Qt::Key_Return}) {
    const QKeyEvent keypad(QEvent::KeyPress, qt_key, Qt::KeypadModifier,
                           0xe01c, 0x0d, 0);
    EXPECT_EQ(quader_input_chord_from_qt_event(keypad).key,
              QuaderKeyCode::NumpadEnter);
  }
  const QKeyEvent main(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier,
                       0x1c, 0x0d, 0);
  EXPECT_EQ(quader_input_chord_from_qt_event(main).key, QuaderKeyCode::Enter);
}

TEST(QtInputAdapterTests, WindowsNativeControlAndPunctuationSlotsAreCanonical) {
  constexpr std::array kMappings{
      std::pair{quint32{0x08}, QuaderKeyCode::Backspace},
      std::pair{quint32{0x09}, QuaderKeyCode::Tab},
      std::pair{quint32{0x1b}, QuaderKeyCode::Escape},
      std::pair{quint32{0x20}, QuaderKeyCode::Space},
      std::pair{quint32{0x21}, QuaderKeyCode::PageUp},
      std::pair{quint32{0x22}, QuaderKeyCode::PageDown},
      std::pair{quint32{0x23}, QuaderKeyCode::End},
      std::pair{quint32{0x24}, QuaderKeyCode::Home},
      std::pair{quint32{0x25}, QuaderKeyCode::Left},
      std::pair{quint32{0x26}, QuaderKeyCode::Up},
      std::pair{quint32{0x27}, QuaderKeyCode::Right},
      std::pair{quint32{0x28}, QuaderKeyCode::Down},
      std::pair{quint32{0x2d}, QuaderKeyCode::Insert},
      std::pair{quint32{0x2e}, QuaderKeyCode::Delete},
      std::pair{quint32{0x6a}, QuaderKeyCode::NumpadMultiply},
      std::pair{quint32{0x6b}, QuaderKeyCode::NumpadAdd},
      std::pair{quint32{0x6d}, QuaderKeyCode::NumpadSubtract},
      std::pair{quint32{0x6e}, QuaderKeyCode::NumpadDecimal},
      std::pair{quint32{0x6f}, QuaderKeyCode::NumpadDivide},
      std::pair{quint32{0xba}, QuaderKeyCode::Semicolon},
      std::pair{quint32{0xbb}, QuaderKeyCode::Equal},
      std::pair{quint32{0xbc}, QuaderKeyCode::Comma},
      std::pair{quint32{0xbd}, QuaderKeyCode::Minus},
      std::pair{quint32{0xbe}, QuaderKeyCode::Period},
      std::pair{quint32{0xbf}, QuaderKeyCode::Slash},
      std::pair{quint32{0xc0}, QuaderKeyCode::Grave},
      std::pair{quint32{0xdb}, QuaderKeyCode::LeftBracket},
      std::pair{quint32{0xdc}, QuaderKeyCode::Backslash},
      std::pair{quint32{0xdd}, QuaderKeyCode::RightBracket},
      std::pair{quint32{0xde}, QuaderKeyCode::Apostrophe},
      std::pair{quint32{0xe2}, QuaderKeyCode::IntlBackslash},
  };
  for (const auto &[native_key, expected] : kMappings) {
    for (const int glyph : {0x00e8, static_cast<int>(Qt::Key_Dead_Acute),
                            static_cast<int>(Qt::Key_BraceLeft)}) {
      SCOPED_TRACE(native_key);
      SCOPED_TRACE(glyph);
      const QKeyEvent event(QEvent::KeyPress, glyph,
                            Qt::ControlModifier | Qt::ShiftModifier, 0,
                            native_key, 0, QStringLiteral("\u00e8"));
      const auto chord = quader_input_chord_from_qt_event(event);
      EXPECT_EQ(chord.key, expected);
      EXPECT_TRUE(chord.primary);
      EXPECT_TRUE(chord.shift);
      EXPECT_EQ(event.text(), QStringLiteral("\u00e8"));
    }
  }
  const QKeyEvent azerty_digit(QEvent::KeyPress, Qt::Key_Ampersand,
                               Qt::NoModifier, 0x02, 0x31, 0,
                               QStringLiteral("&"));
  EXPECT_EQ(quader_input_chord_from_qt_event(azerty_digit).key,
            QuaderKeyCode::Digit1);
  const QKeyEvent international(QEvent::KeyPress, Qt::Key_Less,
                                Qt::NoModifier, 0x56, 0xe2, 0,
                                QStringLiteral("<"));
  const auto captured = quader_input_chord_from_qt_event(international);
  EXPECT_EQ(quader_input_format_chord(captured), "IntlBackslash");
  const auto restored =
      quader_input_parse_chord(quader_input_format_chord(captured));
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->key, captured.key);
}

TEST(QtInputAdapterTests,
     WindowsNativeTopRowDigitsNormalizeItalianShiftedSymbols) {
  constexpr std::array kMappings{
      KeyMapping{Qt::Key_Equal, QuaderKeyCode::Digit0},
      KeyMapping{Qt::Key_Exclam, QuaderKeyCode::Digit1},
      KeyMapping{Qt::Key_QuoteDbl, QuaderKeyCode::Digit2},
      KeyMapping{Qt::Key_sterling, QuaderKeyCode::Digit3},
      KeyMapping{Qt::Key_Dollar, QuaderKeyCode::Digit4},
      KeyMapping{Qt::Key_Percent, QuaderKeyCode::Digit5},
      KeyMapping{Qt::Key_Ampersand, QuaderKeyCode::Digit6},
      KeyMapping{Qt::Key_Slash, QuaderKeyCode::Digit7},
      KeyMapping{Qt::Key_ParenLeft, QuaderKeyCode::Digit8},
      KeyMapping{Qt::Key_ParenRight, QuaderKeyCode::Digit9},
  };
  constexpr quint32 kWindowsDigit0VirtualKey = 0x30;
  const Qt::KeyboardModifiers modifiers =
      Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier;
  for (std::size_t index = 0; index < kMappings.size(); ++index) {
    const KeyMapping &mapping = kMappings[index];
    const quint32 native_key =
        kWindowsDigit0VirtualKey + static_cast<quint32>(index);
    const QString text(QChar(static_cast<quint16>(mapping.qt_key)));
    for (const auto type : {QEvent::KeyPress, QEvent::ShortcutOverride,
                            QEvent::KeyRelease}) {
      SCOPED_TRACE(index);
      SCOPED_TRACE(type);
      const QKeyEvent event(type, mapping.qt_key, modifiers, 0, native_key,
                            0, text);
      const auto chord = quader_input_chord_from_qt_event(event);
      EXPECT_EQ(chord.key, mapping.quader_key);
      EXPECT_TRUE(chord.primary);
      EXPECT_TRUE(chord.shift);
      EXPECT_TRUE(chord.alt);
      EXPECT_EQ(event.key(), mapping.qt_key);
      EXPECT_EQ(event.text(), text);
      EXPECT_EQ(event.nativeVirtualKey(), native_key);
      const auto pressed = quader_pressed_input_chord_from_qt_event(event);
      if (type == QEvent::KeyPress) {
        ASSERT_TRUE(pressed.has_value());
        EXPECT_EQ(pressed->key, mapping.quader_key);
      } else {
        EXPECT_FALSE(pressed.has_value());
      }
    }
    const QKeyEvent unshifted(QEvent::KeyPress,
                              Qt::Key_0 + static_cast<int>(index),
                              Qt::NoModifier, 0, native_key, 0);
    const auto chord = quader_input_chord_from_qt_event(unshifted);
    EXPECT_EQ(chord.key, mapping.quader_key);
    EXPECT_FALSE(chord.shift);
  }
  const QKeyEvent repeat(QEvent::KeyPress, Qt::Key_sterling,
                         Qt::ShiftModifier, 0, 0x33, 0,
                         QStringLiteral("\u00a3"), true);
  EXPECT_FALSE(quader_pressed_input_chord_from_qt_event(repeat).has_value());
}

TEST(QtInputAdapterTests,
     WindowsNativeDigitsPreserveQuoteAndKeypadKeyIdentity) {
  constexpr quint32 kWindowsQuoteVirtualKey = 0xde;
  constexpr quint32 kWindowsNumpad2VirtualKey = 0x62;
  constexpr quint32 kWindowsDigit2VirtualKey = 0x32;
  const QKeyEvent quote(QEvent::KeyPress, Qt::Key_QuoteDbl,
                        Qt::ShiftModifier, 0, kWindowsQuoteVirtualKey, 0,
                        QStringLiteral("\""));
  const auto quote_chord = quader_input_chord_from_qt_event(quote);
  EXPECT_EQ(quote_chord.key, QuaderKeyCode::Apostrophe);
  EXPECT_TRUE(quote_chord.shift);
  for (const auto native_key : {kWindowsNumpad2VirtualKey,
                                kWindowsDigit2VirtualKey}) {
    SCOPED_TRACE(native_key);
    const QKeyEvent keypad(QEvent::KeyPress, Qt::Key_2,
                           Qt::KeypadModifier | Qt::ShiftModifier,
                           0, native_key, 0, QStringLiteral("2"));
    const auto chord = quader_input_chord_from_qt_event(keypad);
    EXPECT_EQ(chord.key, QuaderKeyCode::Numpad2);
    EXPECT_TRUE(chord.shift);
  }
}
#else
TEST(QtInputAdapterTests, NonWindowsNativeKeysRetainQtKeyMapping) {
  const QKeyEvent quote(QEvent::KeyPress, Qt::Key_QuoteDbl,
                        Qt::ShiftModifier, 0, 0x32, 0,
                        QStringLiteral("\""));
  const auto chord = quader_input_chord_from_qt_event(quote);
  EXPECT_EQ(chord.key, QuaderKeyCode::Apostrophe);
  EXPECT_TRUE(chord.shift);
}
#endif

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
  surface.resize(800, 600);

  ASSERT_FALSE(render_system.initialized());
  EXPECT_FALSE(surface.initialize(render_system, 0x01u));
  EXPECT_FALSE(surface.ready_to_render());
  EXPECT_FALSE(surface.initialize(render_system, 0x01u));
  EXPECT_FALSE(surface.ready_to_render());
  EXPECT_FALSE(surface.render_frame());
}

TEST(QtViewportSurfaceCursorPolicyTests,
     HoverAndCapturePrecedenceAreToolkitNeutral) {
  using Cursor = quader::ViewportPointerCursor;
  using Presentation = quader::QtViewportCursorPresentation;

  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, true, false),
            Presentation::PrecisionCross);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, false, false),
            Presentation::Default);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::PrecisionCross, true, true),
            Presentation::Hidden);
  EXPECT_EQ(quader::qt_viewport_cursor_presentation(
                Cursor::Default, true, true),
            Presentation::Hidden);
  for (const auto cursor : {Cursor::OpenHand, Cursor::ClosedHand}) {
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, true, false),
              cursor == Cursor::OpenHand ? Presentation::OpenHand
                                         : Presentation::ClosedHand);
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, false, false),
              Presentation::Default);
    EXPECT_EQ(quader::qt_viewport_cursor_presentation(cursor, false, true),
              Presentation::Hidden);
  }
  for (const auto cursor : {Cursor::SelectionDefault, Cursor::SelectionAdd,
                             Cursor::SelectionSubtract}) {
    const Presentation expected =
        cursor == Cursor::SelectionDefault
            ? Presentation::SelectionDefault
            : (cursor == Cursor::SelectionAdd ? Presentation::SelectionAdd
                                               : Presentation::SelectionSubtract);
    EXPECT_EQ(
        quader::qt_viewport_cursor_presentation(cursor, true, false),
        expected);
    EXPECT_EQ(
        quader::qt_viewport_cursor_presentation(cursor, false, false),
        Presentation::Default);
    EXPECT_EQ(
        quader::qt_viewport_cursor_presentation(cursor, true, true),
        Presentation::Hidden);
  }
}

TEST(QtViewportSurfaceInputTests,
     ComposedWidgetForwardsInputOnceWithoutCreatingASurfaceWindow) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-composed-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget root;
  root.resize(900, 700);
  QWidget host(&root);
  host.setGeometry(30, 40, 640, 480);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective, true);
  surface.set_visible_host(host);
  surface.set_interaction_cursor(quader::ViewportPointerCursor::PrecisionCross);
  const QPointF local{80, 90};
  const QPoint global = host.mapToGlobal(local.toPoint());
  const float scale = static_cast<float>(host.devicePixelRatioF());

  QEnterEvent enter(local, local, global);
  QApplication::sendEvent(&host, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  QApplication::sendEvent(&host, &focus);
  QKeyEvent key_press(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
  QApplication::sendEvent(&host, &key_press);
  QMouseEvent press(QEvent::MouseButtonPress, local, global,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&host, &press);
  const QPointF moved_local = local + QPointF{8, 4};
  const QPoint moved_global = global + QPoint{8, 4};
  QMouseEvent move(QEvent::MouseMove, moved_local, moved_global,
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(&host, &move);
  QWheelEvent wheel(moved_local, moved_global, {}, {0, 120}, Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  QApplication::sendEvent(&host, &wheel);

  const auto first = surface.take_input_frame(1.0f / 60.0f, false);
  EXPECT_TRUE(first.input.viewport_hovered);
  EXPECT_TRUE(first.input.viewport_focused);
  EXPECT_TRUE(first.left_pressed);
  EXPECT_TRUE(first.left_down);
  ASSERT_EQ(first.input.pointer_pressed_edges.size(), 1u);
  ASSERT_EQ(first.input.pressed_input_chords.size(), 2u);
  EXPECT_EQ(std::count_if(first.input.pressed_input_chords.begin(),
                          first.input.pressed_input_chords.end(),
                          [](const QuaderInputChord &chord) {
                            return chord.key == QuaderKeyCode::W;
                          }), 1);
  EXPECT_EQ(std::count_if(first.input.pressed_input_chords.begin(),
                          first.input.pressed_input_chords.end(),
                          [](const QuaderInputChord &chord) {
                            return chord.device == QuaderInputDevice::Mouse &&
                                   chord.mouse_button == QuaderMouseButton::Left &&
                                   chord.gesture == QuaderInputGesture::Press;
                          }), 1);
  EXPECT_EQ(first.input.wheel_input_edges.size(), 2u);
  EXPECT_FLOAT_EQ(first.input.mouse_local.x, 88 * scale);
  EXPECT_FLOAT_EQ(first.input.mouse_local.y, 94 * scale);
  EXPECT_FLOAT_EQ(first.input.mouse_delta.x, 8 * scale);
  EXPECT_FLOAT_EQ(first.input.mouse_delta.y, 4 * scale);
  EXPECT_FLOAT_EQ(first.input.viewport_size.x, 640 * scale);
  EXPECT_FLOAT_EQ(first.input.viewport_size.y, 480 * scale);
  EXPECT_EQ(surface.visible_local_to_global(local.toPoint()), global);
  EXPECT_EQ(host.cursor().shape(), Qt::CrossCursor);
  EXPECT_EQ(surface.handle(), nullptr);
  EXPECT_EQ(host.internalWinId(), 0u);
  EXPECT_EQ(surface.presentation_geometry_query_count(), 0u);

  const auto unchanged = surface.take_input_frame(1.0f / 60.0f, false);
  EXPECT_TRUE(unchanged.left_down);
  EXPECT_FALSE(unchanged.left_pressed);
  EXPECT_EQ(unchanged.mouse_move_events, 0u);
  EXPECT_TRUE(unchanged.input.pointer_pressed_edges.empty());
  EXPECT_TRUE(unchanged.input.pressed_input_chords.empty());
  EXPECT_TRUE(unchanged.input.wheel_input_edges.empty());

  QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&host, &release);
  QKeyEvent key_release(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier);
  QApplication::sendEvent(&host, &key_release);
  const auto released = surface.take_input_frame(1.0f / 60.0f, false);
  EXPECT_TRUE(released.left_released);
  EXPECT_FALSE(released.left_down);
  EXPECT_EQ(released.input.pointer_released_edges.size(), 1u);
  EXPECT_EQ(released.input.released_input_chords.size(), 1u);
  QFocusEvent blur(QEvent::FocusOut);
  QApplication::sendEvent(&host, &blur);
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&host, &leave);
  const auto abandoned = surface.take_input_frame(1.0f / 60.0f, false);
  EXPECT_FALSE(abandoned.input.viewport_focused);
  EXPECT_FALSE(abandoned.input.viewport_hovered);
  EXPECT_EQ(host.cursor().shape(), Qt::ArrowCursor);
  EXPECT_EQ(surface.handle(), nullptr);
}

TEST(QtViewportSurfaceInputTests,
     FixedBackingCropRetainsVisibleInputAndPopupCoordinates) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-fixed-backing-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QWidget root;
  root.setAttribute(Qt::WA_DontShowOnScreen);
  root.resize(1000, 900);
  QWidget visible_host(&root);
  visible_host.setAttribute(Qt::WA_NativeWindow);
  visible_host.setGeometry(40, 60, 640, 480);
  auto *surface = new quader::QtViewportSurface(quader::ViewportPaneId::Perspective);
  auto *backing = QWidget::createWindowContainer(surface, &visible_host);
  backing->setAttribute(Qt::WA_NativeWindow);
  backing->setGeometry(-160, -160, 960, 800);
  surface->set_visible_host(visible_host);
  root.show();
  QApplication::processEvents();
  ASSERT_EQ(surface->size(), QSize(960, 800));
  const auto native_id = surface->winId();
  (void)surface->take_input_frame(1.0f / 60.0f, false);
  const auto settled_queries = surface->presentation_geometry_query_count();
  ASSERT_GT(settled_queries, 0u);
  for (int frame = 0; frame < 32; ++frame) {
    (void)surface->render_size_pixels();
    surface->synchronize_render_size();
    (void)surface->take_input_frame(1.0f / 60.0f, false);
  }
  EXPECT_EQ(surface->presentation_geometry_query_count(), settled_queries);
  const float scale = static_cast<float>(visible_host.devicePixelRatioF());
  const QPointF visible{80, 90};
  const QPoint global = visible_host.mapToGlobal(visible.toPoint());
  const QPoint raw = surface->mapFromGlobal(global);
  ASSERT_NE(raw, visible.toPoint());
  EXPECT_EQ(surface->visible_local_to_global(visible.toPoint()), global);
  EXPECT_NE(surface->mapToGlobal(visible.toPoint()), global);

  QEnterEvent enter(visible, visible, global);
  send_visible_viewport_event(surface, &enter);
  const auto entered = surface->take_input_frame(1.0f / 60.0f, false);
  EXPECT_TRUE(entered.input.viewport_hovered);
  EXPECT_FLOAT_EQ(entered.input.viewport_size.x, 640 * scale);
  EXPECT_FLOAT_EQ(entered.input.viewport_size.y, 480 * scale);
  EXPECT_FLOAT_EQ(entered.input.mouse_local.x, 80 * scale);
  EXPECT_FLOAT_EQ(entered.input.mouse_local.y, 90 * scale);
  EXPECT_FLOAT_EQ(entered.input.mouse_screen.x, global.x() * scale);
  EXPECT_FLOAT_EQ(entered.input.mouse_screen.y, global.y() * scale);

  QWheelEvent wheel(visible, global, {}, {0, 120}, Qt::NoButton,
                    Qt::NoModifier, Qt::NoScrollPhase, false);
  send_visible_viewport_event(surface, &wheel);
  const auto wheeled = surface->take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(wheeled.input.wheel_input_edges.size(), 2u);
  for (const auto &edge : wheeled.input.wheel_input_edges) {
    EXPECT_TRUE(edge.has_local_position);
    EXPECT_FLOAT_EQ(edge.local.x, 80 * scale);
    EXPECT_FLOAT_EQ(edge.local.y, 90 * scale);
  }

  QMouseEvent press(QEvent::MouseButtonPress, visible, global,
                    Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &press);
  const auto pressed = surface->take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(pressed.input.pointer_pressed_edges.size(), 1u);
  EXPECT_FLOAT_EQ(pressed.left_click_local.x, 80 * scale);
  EXPECT_FLOAT_EQ(pressed.left_click_local.y, 90 * scale);

  // Growing the clip moves its centered backing without moving the pointer.
  visible_host.resize(700, 540);
  backing->move(-130, -130);
  surface->synchronize_render_size();
  EXPECT_EQ(surface->size(), QSize(960, 800));
  EXPECT_EQ(surface->winId(), native_id);
  (void)surface->render_size_pixels();
  EXPECT_GT(surface->presentation_geometry_query_count(), settled_queries);
  EXPECT_NE(surface->mapFromGlobal(global), raw);
  QMouseEvent move(QEvent::MouseMove, visible, global,
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &move);
  const auto moved = surface->take_input_frame(1.0f / 60.0f, false);
  EXPECT_FLOAT_EQ(moved.input.viewport_size.x, 700 * scale);
  EXPECT_FLOAT_EQ(moved.input.viewport_size.y, 540 * scale);
  EXPECT_FLOAT_EQ(moved.input.mouse_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(moved.input.mouse_delta.y, 0.0f);
  EXPECT_TRUE(moved.input.pointer_drag_started_edges.empty());
  EXPECT_FLOAT_EQ(moved.input.mouse_local.x, 80 * scale);
  EXPECT_FLOAT_EQ(moved.input.mouse_local.y, 90 * scale);
  EXPECT_EQ(surface->visible_local_to_global(visible.toPoint()), global);

  QMouseEvent release(QEvent::MouseButtonRelease, visible, global,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &release);
  const auto released = surface->take_input_frame(1.0f / 60.0f, false);
  ASSERT_EQ(released.input.pointer_released_edges.size(), 1u);
  ASSERT_EQ(released.input.pressed_input_chords.size(), 1u);
  EXPECT_EQ(released.input.pressed_input_chords.front().gesture,
            quader::QuaderInputGesture::Click);
  EXPECT_FLOAT_EQ(released.input.pointer_released_edges.front().local.x, 80 * scale);
  EXPECT_FLOAT_EQ(released.input.pointer_released_edges.front().local.y, 90 * scale);

  // Hidden backing pixels beyond the client edge cannot hover the viewport.
  for (const QPointF outside : {QPointF{-1, 90}, QPointF{700, 90},
                                QPointF{80, -1}, QPointF{80, 540}}) {
    SCOPED_TRACE(::testing::Message() << outside.x() << "," << outside.y());
    QMouseEvent outside_move(QEvent::MouseMove, outside, {}, Qt::NoButton,
                             Qt::NoButton, Qt::NoModifier);
    send_visible_viewport_event(surface, &outside_move);
    EXPECT_FALSE(surface->take_input_frame(1.0f / 60.0f, false)
                     .input.viewport_hovered);
  }
}

TEST(QtViewportSurfaceInputTests,
     SelectionCursorsUsePixelExactAuthoredAssetsAndTipAlignedHotspots) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char name[] = "quader-selection-cursor-tests";
  char *argv[] = {name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  surface.resize(800, 600);
  QEnterEvent enter(QPointF{40, 40}, QPointF{40, 40}, QPointF{40, 40});
  QFocusEvent focus_in(QEvent::FocusIn);
  QApplication::sendEvent(&surface, &enter);
  QApplication::sendEvent(&surface, &focus_in);

  const qreal scale = std::max<qreal>(1.0, surface.devicePixelRatio());
  const auto expected_pixmap = [scale](
                                   quader::QtApplicationCursorVariant variant) {
    return quader::qt_application_cursor_pixmap(variant, scale);
  };

  surface.set_interaction_cursor(
      quader::ViewportPointerCursor::SelectionDefault);
  EXPECT_EQ(surface.cursor().shape(), Qt::BitmapCursor);
  const QPixmap selection_default = surface.cursor().pixmap();
  const QPixmap expected_default =
      expected_pixmap(quader::QtApplicationCursorVariant::Default);
  ASSERT_FALSE(selection_default.isNull());
  ASSERT_FALSE(expected_default.isNull());
  EXPECT_EQ(selection_default.toImage(), expected_default.toImage());
  EXPECT_EQ(surface.cursor().hotSpot(), QPoint(0, 0));

  surface.set_interaction_cursor(quader::ViewportPointerCursor::SelectionAdd);
  const QPixmap additive = surface.cursor().pixmap();
  const QPixmap expected_add =
      expected_pixmap(quader::QtApplicationCursorVariant::SelectionAdd);
  ASSERT_FALSE(additive.isNull());
  ASSERT_FALSE(expected_add.isNull());
  EXPECT_EQ(additive.toImage(), expected_add.toImage());
  EXPECT_EQ(surface.cursor().hotSpot(), QPoint(0, 0));
  EXPECT_DOUBLE_EQ(additive.devicePixelRatio(), scale);
  const auto stable_revision = surface.pointer_cursor_presentation_revision();
  surface.set_interaction_cursor(quader::ViewportPointerCursor::SelectionAdd);
  EXPECT_EQ(surface.pointer_cursor_presentation_revision(), stable_revision);

  surface.set_interaction_cursor(
      quader::ViewportPointerCursor::SelectionSubtract);
  EXPECT_EQ(surface.cursor().shape(), Qt::BitmapCursor);
  const QPixmap subtractive = surface.cursor().pixmap();
  const QPixmap expected_subtract = expected_pixmap(
      quader::QtApplicationCursorVariant::SelectionSubtract);
  ASSERT_FALSE(subtractive.isNull());
  ASSERT_FALSE(expected_subtract.isNull());
  EXPECT_EQ(subtractive.toImage(), expected_subtract.toImage());
  EXPECT_EQ(surface.cursor().hotSpot(), QPoint(0, 0));
  EXPECT_NE(selection_default.cacheKey(), additive.cacheKey());
  EXPECT_NE(additive.cacheKey(), subtractive.cacheKey());

  QFocusEvent focus_out(QEvent::FocusOut);
  QApplication::sendEvent(&surface, &focus_out);
  EXPECT_EQ(surface.cursor().shape(), Qt::BitmapCursor);
  QApplication::sendEvent(&surface, &focus_in);
  EXPECT_EQ(surface.cursor().shape(), Qt::BitmapCursor);
  QEvent leave(QEvent::Leave);
  QApplication::sendEvent(&surface, &leave);
  EXPECT_EQ(surface.cursor().shape(), Qt::ArrowCursor);
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
  surface.resize(800, 600);
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
    EXPECT_EQ(surface.cursor().shape(),
              cursor == quader::ViewportPointerCursor::OpenHand
                  ? Qt::OpenHandCursor : Qt::ClosedHandCursor);
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
     PrecisionCursorFollowsHoverBeforeFocusAndCachesUpdates) {
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
  EXPECT_EQ(surface.cursor().shape(), Qt::CrossCursor);
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
  EXPECT_EQ(surface.cursor().shape(), Qt::CrossCursor);
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
     PrimitiveHoverKeepsTextFocusAndFirstPressDrawsInTheActivePane) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-primitive-first-press-focus-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const auto layout : {quader::ViewportLayoutId::Single,
                            quader::ViewportLayoutId::FourQuad}) {
    for (const bool coalesced : {false, true}) {
      SCOPED_TRACE(static_cast<int>(layout));
      SCOPED_TRACE(coalesced);
      quader::SceneSession session;
      quader::QuaderEditorApi editor(
          quader::QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      auto &workspace = session.viewport_workspace();
      (void)workspace.set_layout(layout);

      QWidget host;
      host.setAttribute(Qt::WA_DontShowOnScreen, true);
      host.resize(1000, 650);
      QLineEdit field(&host);
      field.setGeometry(0, 0, 180, 30);
      field.setText(QStringLiteral("material search draft"));
      quader::QtViewportPanel panel(&host);
      panel.setGeometry(200, 0, 800, 600);
      panel.attach_workspace(workspace);
      panel.set_startup_presentation_blocked(false);
      for (const auto &placement : workspace.active_pane_placements()) {
        auto *container = panel.findChild<QWidget *>(
            QStringLiteral("viewportSurfaceContainer_%1").arg(
                QString::fromUtf8(
                    quader::viewport_pane_stable_id(placement.pane_id).data())));
        ASSERT_NE(container, nullptr);
        // The renderer normally reveals native children after startup.
        container->show();
      }
      host.show();
      QApplication::setActiveWindow(&host);
      field.setFocus();
      ASSERT_EQ(QApplication::focusWidget(), &field);
      for (const auto &placement : workspace.active_pane_placements()) {
        ASSERT_TRUE(editor.viewports().focus_pane(placement.pane_id).ok());
        ASSERT_TRUE(editor.tools().activate(
            quader::ViewportToolId::CreatePrimitive).ok());
      }
      ASSERT_TRUE(editor.viewports().focus_pane(
          quader::ViewportPaneId::Perspective).ok());
      ASSERT_TRUE(editor.tools().activate(
          quader::ViewportToolId::CreatePrimitive).ok());
      const auto primitive_origin =
          editor.tools().active_settings_presentation_origin();

      const auto process_input = [&] {
        panel.process_input(
            editor, session.scene(), session.assets(), session.modeling(), {},
            session.native_tools(), &session.point_placement(), 1.0 / 60.0);
      };
      const std::array panes = {quader::ViewportPaneId::Perspective,
                                quader::ViewportPaneId::Top,
                                quader::ViewportPaneId::Front,
                                quader::ViewportPaneId::Side};
      const std::size_t pane_count =
          layout == quader::ViewportLayoutId::Single ? 1u : panes.size();
      quader::QtViewportSurface *previous = nullptr;
      for (std::size_t index = 0; index < pane_count; ++index) {
        auto *surface = panel.surface(panes[index]);
        ASSERT_NE(surface, nullptr);
        resize_viewport_surface(panel, *surface, {800, 600});
        if (previous != nullptr) {
          QEvent leave(QEvent::Leave);
          send_visible_viewport_event(previous, &leave);
        }
        const QPointF local{300, 300};
        const QPointF global(surface->mapToGlobal(local.toPoint()));
        QEnterEvent enter(local, local, global);
        send_visible_viewport_event(surface, &enter);
        process_input();
        EXPECT_EQ(workspace.focused_pane(), panes[index]);
        EXPECT_EQ(QApplication::focusWidget(), &field);
        EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);
        EXPECT_EQ(editor.tools().active_settings_presentation_origin(),
                  primitive_origin);
        if (previous != nullptr) {
          EXPECT_EQ(previous->cursor().shape(), Qt::ArrowCursor);
        }
        const auto cursor_revision =
            surface->pointer_cursor_presentation_revision();
        process_input();
        EXPECT_EQ(surface->pointer_cursor_presentation_revision(),
                  cursor_revision);
        EXPECT_EQ(session.primitive_creation().shape_count(), 0u);
        previous = surface;
      }

      const auto active_pane = panes[pane_count - 1u];
      auto *surface = panel.surface(active_pane);
      ASSERT_NE(surface, nullptr);
      (void)workspace.set_view(active_pane, quader::ViewportView::Top);
      workspace.camera(active_pane).focus_origin(16.0f);
      process_input();
      const auto send_mouse = [&](QEvent::Type type, QPointF local,
                                  Qt::MouseButton button,
                                  Qt::MouseButtons held) {
        QMouseEvent event(type, local,
                         QPointF(surface->mapToGlobal(local.toPoint())),
                         button, held, Qt::NoModifier);
        send_visible_viewport_event(surface, &event);
      };
      send_mouse(QEvent::MouseButtonPress, {300, 300}, Qt::LeftButton,
                 Qt::LeftButton);
      EXPECT_NE(QApplication::focusWidget(), &field);
      EXPECT_EQ(field.text(), QStringLiteral("material search draft"));
      if (!coalesced) {
        process_input();
        EXPECT_EQ(session.modeling().box_drag_state(),
                  quader::ModelingBoxDragState::Footprint);
        EXPECT_EQ(session.primitive_creation().shape_count(), 0u);
      }
      send_mouse(QEvent::MouseButtonRelease, {500, 430}, Qt::LeftButton,
                 Qt::NoButton);
      process_input();
      ASSERT_TRUE(editor.tools().live_primitive_creation().has_value());
      EXPECT_EQ(session.primitive_creation().shape_count(), 1u);
      EXPECT_EQ(session.modeling().box_drag_state(),
                quader::ModelingBoxDragState::Idle);
      EXPECT_EQ(workspace.focused_pane(), active_pane);
      EXPECT_EQ(editor.query().modeling_objects().size(), 1u);
    }
  }
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
  resize_viewport_surface(panel, *surface, {800, 600});

  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };

  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::BitmapCursor);
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);
  auto *inactive_surface = panel.surface(quader::ViewportPaneId::Top);
  ASSERT_NE(inactive_surface, nullptr);
  QEnterEvent inactive_enter(local, local, global);
  send_visible_viewport_event(inactive_surface, &inactive_enter);
  send_visible_viewport_event(inactive_surface, &focus);
  EXPECT_EQ(inactive_surface->cursor().shape(), Qt::ArrowCursor);
  QEvent inactive_leave(QEvent::Leave);
  QFocusEvent inactive_focus_out(QEvent::FocusOut);
  send_visible_viewport_event(inactive_surface, &inactive_leave);
  send_visible_viewport_event(inactive_surface, &inactive_focus_out);
  const std::uint64_t stable_revision =
      surface->pointer_cursor_presentation_revision();
  process_input();
  EXPECT_EQ(surface->pointer_cursor_presentation_revision(), stable_revision);

  QMouseEvent orbit_press(QEvent::MouseButtonPress, local, global,
                          Qt::MiddleButton, Qt::MiddleButton,
                          Qt::NoModifier);
  send_visible_viewport_event(surface, &orbit_press);
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);

  QMouseEvent orbit_release(QEvent::MouseButtonRelease, local, global,
                            Qt::MiddleButton, Qt::NoButton,
                            Qt::NoModifier);
  send_visible_viewport_event(surface, &orbit_release);
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::BitmapCursor);

  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kSceneCreatePointLight)
                  .ok());
  process_input();
  EXPECT_EQ(editor.tools().active_tool(), "create_point_light");
  EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);

  ASSERT_TRUE(editor.commands()
                  .execute(quader::quader_command::kSceneCreateDirectionalLight)
                  .ok());
  process_input();
  EXPECT_EQ(editor.tools().active_tool(), "create_directional_light");
  EXPECT_EQ(surface->cursor().shape(), Qt::CrossCursor);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::BitmapCursor);
}

TEST(QtViewportSurfaceInputTests,
     ExternalSelectionModeChangeClearsMarqueeAndPreservesTheNextClick) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-external-selection-mode-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Front).ok());
  session.history().clear();

  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(panel, *surface, {800, 600});
  session.viewport_workspace().camera(quader::ViewportPaneId::Perspective)
      .focus_origin(8.0f);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  const auto process_input = [&] {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  const auto send_mouse = [&](QEvent::Type type, QPointF local,
                              Qt::MouseButton button, Qt::MouseButtons held,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, local,
                     QPointF(surface->mapToGlobal(local.toPoint())),
                     button, held, modifiers);
    send_visible_viewport_event(surface, &event);
    process_input();
  };
  const auto expect_cursor = [&](quader::QtApplicationCursorVariant variant) {
    ASSERT_EQ(surface->cursor().shape(), Qt::BitmapCursor);
    EXPECT_EQ(surface->cursor().pixmap().toImage(),
              quader::qt_application_cursor_pixmap(
                  variant, std::max<qreal>(1.0, surface->devicePixelRatio()))
                  .toImage());
  };
  process_input();
  send_mouse(QEvent::MouseButtonPress, {100, 100}, Qt::LeftButton,
             Qt::LeftButton, Qt::ShiftModifier);
  send_mouse(QEvent::MouseMove, {700, 500}, Qt::NoButton,
             Qt::LeftButton, Qt::ShiftModifier);
  QKeyEvent release_shift(QEvent::KeyRelease, Qt::Key_Shift, Qt::ShiftModifier);
  send_visible_viewport_event(surface, &release_shift);
  process_input();
  expect_cursor(quader::QtApplicationCursorVariant::SelectionAdd);

  const auto active_tool_revision = editor.tools().active_tool_revision();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_EQ(editor.tools().active_tool_revision(), active_tool_revision);
  const auto history_after_mode_change = editor.history().snapshot();
  const auto content_revision = session.modeling().content_revision();
  process_input();
  EXPECT_EQ(panel.tool_state().selection_mode, quader::ViewportSelectionMode::Face);
  EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::Select);
  expect_cursor(quader::QtApplicationCursorVariant::Default);
  send_mouse(QEvent::MouseButtonRelease, {700, 500}, Qt::LeftButton,
             Qt::NoButton);
  EXPECT_EQ(editor.selection().summary().selected_face_count, 0u);
  EXPECT_EQ(session.modeling().content_revision(), content_revision);
  expect_history_unchanged(editor.history().snapshot(), history_after_mode_change);

  send_mouse(QEvent::MouseButtonPress, {400, 300}, Qt::LeftButton, Qt::LeftButton);
  send_mouse(QEvent::MouseButtonRelease, {400, 300}, Qt::LeftButton, Qt::NoButton);
  const auto selected = editor.selection().summary();
  EXPECT_EQ(selected.mode, quader::QuaderSelectionMode::Face);
  EXPECT_EQ(selected.active_object_id, box.value().modeling_object_id);
  EXPECT_EQ(selected.selected_face_count, 1u);
}

TEST(QtViewportSurfaceInputTests,
     MouseReleaseRetainsRemappedVertexSnapAcrossCoalescedKeyTransitions) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-release-held-snap-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
  const quader::ViewportToolState state{
      .active_tool = quader::ViewportToolId::Extrude,
      .selection_mode = quader::ViewportSelectionMode::Face};
  const auto capture = [](int key, bool held_at_release) {
    quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
    surface.resize(800, 600);
    const auto send_key = [&](QEvent::Type type) {
      QKeyEvent event(type, key, Qt::NoModifier);
      QApplication::sendEvent(&surface, &event);
    };
    const auto send_mouse = [&](QEvent::Type type, Qt::MouseButtons held) {
      QMouseEvent event(type, QPointF{120, 180}, QPointF{220, 280},
                        Qt::LeftButton, held, Qt::NoModifier);
      QApplication::sendEvent(&surface, &event);
    };
    send_key(QEvent::KeyPress);
    send_mouse(QEvent::MouseButtonPress, Qt::LeftButton);
    (void)surface.take_input_frame(0.0F, false);
    if (!held_at_release) {
      send_key(QEvent::KeyRelease);
    }
    send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
    send_key(held_at_release ? QEvent::KeyRelease : QEvent::KeyPress);
    return surface.take_input_frame(0.0F, false).input;
  };
  const auto resolve = [&](quader::ViewportInputFrame &input,
                           std::vector<quader::QuaderInputChord> &suppressed) {
    double linear_remainder = 0.0;
    double radial_remainder = 0.0;
    quader::ViewportKeybindingResolver::resolve(
        editor, state, input, suppressed, linear_remainder, radial_remainder);
  };
  for (const int key : {Qt::Key_V, Qt::Key_F18}) {
    SCOPED_TRACE(key);
    ASSERT_TRUE(editor.preferences().set_shortcut(
        "viewport.transform.vertex_snap", key == Qt::Key_V ? "V" : "F18").ok());
    for (const bool held_at_release : {true, false}) {
      SCOPED_TRACE(held_at_release);
      auto input = capture(key, held_at_release);
      ASSERT_EQ(input.pointer_released_edges.size(), 1u);
      ASSERT_TRUE(input.pointer_released_edges.front().held_input.has_value());
      EXPECT_EQ(input.pointer_released_edges.front().held_input->active_input_chords.size(),
                held_at_release ? 1u : 0u);
      const auto raw = input;
      std::vector<quader::QuaderInputChord> suppressed;
      resolve(input, suppressed);
      EXPECT_EQ(input.transform_vertex_snap_down, !held_at_release);
      EXPECT_EQ(input.pointer_released_edges.front().held_input->transform_vertex_snap_down,
                held_at_release);
      if (!held_at_release) {
        continue;
      }

      auto suppressed_input = raw;
      suppressed = raw.pointer_released_edges.front().held_input->active_input_chords;
      resolve(suppressed_input, suppressed);
      EXPECT_FALSE(suppressed_input.pointer_released_edges.front()
                       .held_input->transform_vertex_snap_down);
      EXPECT_TRUE(suppressed.empty()) << "Key-up retires suppression after the snapshot resolves.";
      for (int blocked_context = 0; blocked_context < 3; ++blocked_context) {
        auto blocked = raw;
        blocked.viewport_focused = blocked_context != 0;
        blocked.text_input_active = blocked_context == 1;
        blocked.navigation_active = blocked_context == 2;
        blocked.pointer_released_edges.front().held_input->transform_vertex_snap_down = true;
        resolve(blocked, suppressed);
        EXPECT_FALSE(blocked.pointer_released_edges.front()
                         .held_input->transform_vertex_snap_down);
      }
    }
  }
  auto old_binding = capture(Qt::Key_V, true);
  std::vector<quader::QuaderInputChord> suppressed;
  resolve(old_binding, suppressed);
  EXPECT_FALSE(old_binding.pointer_released_edges.front()
                   .held_input->transform_vertex_snap_down);
}

TEST(QtViewportSurfaceInputTests,
     FirstFaceSelectionLocksExtrusionUntilExplicitApplyOrCancelThroughTheProductionPanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-repeated-extrude-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const bool coalesced : {false, true}) {
    SCOPED_TRACE(coalesced);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    const auto box = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
    ASSERT_TRUE(box.ok()) << box.error_message();
    const auto other = editor.modeling().create_box(
        {.min = {3.0F, -1.0F, -1.0F}, .max = {5.0F, 1.0F, 1.0F}});
    ASSERT_TRUE(other.ok()) << other.error_message();
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Front).ok());

    quader::QtViewportPanel panel;
    panel.attach_workspace(session.viewport_workspace());
    auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
    ASSERT_NE(surface, nullptr);
    resize_viewport_surface(panel, *surface, {800, 600});
    session.viewport_workspace().camera(quader::ViewportPaneId::Perspective)
        .focus_origin(12.0f);
    const QPointF local{400.0, 300.0};
    const QPointF global{500.0, 400.0};
    QEnterEvent enter(local, local, global);
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &enter);
    send_visible_viewport_event(surface, &focus);
    const auto process_input = [&] {
      panel.process_input(
          editor, session.scene(), session.assets(), session.modeling(), {},
          session.native_tools(), &session.point_placement(), 1.0 / 60.0);
    };
    const auto press_extrude = [&] {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_E, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_E, Qt::NoModifier);
      send_visible_viewport_event(surface, &press);
      if (!coalesced) {
        process_input();
      }
      send_visible_viewport_event(surface, &release);
      process_input();
    };
    const auto press_key = [&](int key) {
      QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
      send_visible_viewport_event(surface, &press);
      send_visible_viewport_event(surface, &release);
      process_input();
    };
    const auto click = [&](QPointF position,
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
      const auto screen = QPointF(surface->mapToGlobal(position.toPoint()));
      QMouseEvent press(QEvent::MouseButtonPress, position, screen,
                        Qt::LeftButton, Qt::LeftButton, modifiers);
      QMouseEvent release(QEvent::MouseButtonRelease, position, screen,
                          Qt::LeftButton, Qt::NoButton, modifiers);
      send_visible_viewport_event(surface, &press);
      if (!coalesced) process_input();
      send_visible_viewport_event(surface, &release);
      process_input();
    };
    process_input();
    session.history().clear();
    const auto original = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(original.has_value());

    press_extrude();
    ASSERT_EQ(editor.tools().active_tool(), "extrude");
    EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::Extrude);
    EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(session.history().undo_count(), 0u);

    QMouseEvent select_press(QEvent::MouseButtonPress, local, global,
                             Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent select_release(QEvent::MouseButtonRelease, local, global,
                               Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    send_visible_viewport_event(surface, &select_press);
    if (!coalesced) {
      process_input();
    }
    send_visible_viewport_event(surface, &select_release);
    process_input();
    ASSERT_EQ(editor.selection().summary().selected_face_count, 1u);
    ASSERT_EQ(editor.selection().summary().active_object_id,
              box.value().modeling_object_id);
    ASSERT_TRUE(session.extrude().preview_active());
    EXPECT_DOUBLE_EQ(session.extrude().amount(), 0.0);
    const auto selected = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected->indices, original->indices);
    EXPECT_EQ(selected->vertices.size(), original->vertices.size());
    const auto steps_before_extrude = session.history().undo_count();
    const auto locked_selection = editor.selection().summary();
    const auto projected_other = session.viewport_workspace()
        .camera(quader::ViewportPaneId::Perspective).project_world_point({4, 0, 0});
    ASSERT_TRUE(projected_other.visible);
    const QPointF other_position{
        projected_other.position.x / surface->devicePixelRatio(),
        projected_other.position.y / surface->devicePixelRatio()};
    for (const auto modifiers : {Qt::KeyboardModifiers{},
                                  Qt::KeyboardModifiers{Qt::ShiftModifier},
                                  Qt::KeyboardModifiers{Qt::ControlModifier}}) {
      click(other_position, modifiers);
      EXPECT_EQ(editor.selection().summary().active_object_id,
                locked_selection.active_object_id);
      EXPECT_EQ(editor.selection().summary().active_face_id,
                locked_selection.active_face_id);
      EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    }
    click({790, 590});
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 2.15).ok());
    press_extrude();
    const auto first = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(first.has_value());
    EXPECT_GT(first->indices.size(), original->indices.size());
    EXPECT_EQ(session.history().undo_count(), steps_before_extrude);
    EXPECT_TRUE(editor.tools().active_settings().preview_active);
    EXPECT_EQ(editor.selection().summary().selected_face_count, 1u);
    ASSERT_EQ(editor.tools().active_tool(), "extrude");

    press_extrude();
    const auto second = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(second.has_value());
    expect_compiled_mesh_equal(*second, *first);
    EXPECT_EQ(session.history().undo_count(), steps_before_extrude);
    EXPECT_TRUE(editor.tools().active_settings().preview_active);
    EXPECT_EQ(editor.tools().active_tool(), "extrude");
    press_key(Qt::Key_Space);
    EXPECT_FALSE(editor.tools().active_settings().preview_active);
    EXPECT_DOUBLE_EQ(session.extrude().amount(), 2.15);
    EXPECT_EQ(session.history().undo_count(), steps_before_extrude + 1u);
    click(other_position);
    ASSERT_EQ(editor.selection().summary().active_object_id,
              other.value().modeling_object_id);
    ASSERT_TRUE(session.extrude().preview_active());
    EXPECT_DOUBLE_EQ(session.extrude().amount(), 2.15);
    press_key(Qt::Key_Escape);
    EXPECT_FALSE(session.extrude().preview_active());
    EXPECT_EQ(session.history().undo_count(), steps_before_extrude + 1u);
    ASSERT_TRUE(editor.history().undo().ok());
    const auto undo_first = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(undo_first.has_value());
    EXPECT_EQ(undo_first->indices, original->indices);
    EXPECT_EQ(undo_first->vertices.size(), original->vertices.size());
    EXPECT_EQ(session.history().undo_count(), steps_before_extrude);
    ASSERT_TRUE(editor.history().redo().ok());
    const auto redo_first = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(redo_first.has_value());
    EXPECT_EQ(redo_first->indices, first->indices);
    EXPECT_EQ(redo_first->vertices.size(), first->vertices.size());
  }
}

TEST(QtViewportSurfaceInputTests,
     PreviewCompletionKeysApplyOrCancelGeometryThroughTheProductionPanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-preview-completion-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  enum class Preview { Extrude, ReleasedExtrudeGizmo, Bevel, UvPeel };
  for (const auto kind : {Preview::Extrude, Preview::ReleasedExtrudeGizmo,
                          Preview::Bevel, Preview::UvPeel}) {
    for (const int completion : {Qt::Key_Space, Qt::Key_Escape, Qt::Key_F18}) {
      SCOPED_TRACE(static_cast<int>(kind));
      SCOPED_TRACE(completion);
      NavigationPanelTestContext context;
      ASSERT_NE(context.surface, nullptr);
      auto &editor = context.editor;
      auto &session = context.session;
      const auto created = [&] {
        if (kind != Preview::UvPeel) {
          return editor.modeling().create_box(
              {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Completion source");
        }
        quader_poly::Document document;
        const std::array vertices{
            quader_poly::add_vertex(document, {-2, -1, 0}),
            quader_poly::add_vertex(document, {0, -1, 0}),
            quader_poly::add_vertex(document, {2, -1, 0}),
            quader_poly::add_vertex(document, {-2, 1, 0}),
            quader_poly::add_vertex(document, {0, 1, 0}),
            quader_poly::add_vertex(document, {2, 1, 0})};
        (void)quader_poly::add_face(document,
            std::array{vertices[0], vertices[1], vertices[4], vertices[3]});
        (void)quader_poly::add_face(document,
            std::array{vertices[1], vertices[2], vertices[5], vertices[4]});
        return editor.modeling().create_mesh(
            quader::QuaderPolygonDocumentInternalAccess::from_backend(
                std::move(document)), {.name = "Peel completion source"});
      }();
      ASSERT_TRUE(created.ok()) << created.error_message();
      if (kind == Preview::Bevel) {
        ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
      } else if (kind == Preview::UvPeel) {
        ASSERT_TRUE(editor.selection().select_edge(created.value(), 2, 5).ok());
      } else {
        ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
      }
      const auto source = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(source.has_value());
      session.history().clear();
      context.process();
      if (kind == Preview::Bevel) {
        ASSERT_TRUE(editor.commands().execute(
            editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
        ASSERT_TRUE(editor.tools().set_setting(
            "edge_bevel", "operation.edge_bevel.width", 0.2).ok());
      } else if (kind == Preview::UvPeel) {
        ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
        ASSERT_TRUE(editor.tools().set_setting(
            "uv_peel", "uv_peel.u_repeat", 2.0).ok());
        ASSERT_TRUE(editor.tools().set_setting(
            "uv_peel", "uv_peel.u_offset", 0.25).ok());
        ASSERT_TRUE(session.uv_peel().refresh().ok);
      } else {
        context.key(Qt::Key_E);
        ASSERT_TRUE(session.extrude().preview_active());
        if (kind == Preview::Extrude) {
          ASSERT_TRUE(editor.tools().set_setting(
              "extrude", "extrude.amount", 2.15).ok());
        }
        if (kind == Preview::ReleasedExtrudeGizmo) {
          context.camera.focus_origin(8.0F);
          context.camera.set_view_direction(
              session.extrude().handle_state().world_direction * -1.0F);
          context.process();
          const auto layout = quader::build_extrude_handle_layout(
              context.camera, session.extrude().handle_state(),
              static_cast<float>(context.surface->devicePixelRatio()));
          ASSERT_TRUE(layout.valid);
          const QPointF press{
              layout.button_center_screen.x / context.surface->devicePixelRatio(),
              layout.button_center_screen.y / context.surface->devicePixelRatio()};
          context.mouse(QEvent::MouseButtonPress, press,
                        Qt::LeftButton, Qt::LeftButton);
          context.process();
          context.mouse(QEvent::MouseMove, press + QPointF{0, -30},
                        Qt::NoButton, Qt::LeftButton);
          context.process();
          ASSERT_TRUE(session.extrude().dragging());
          context.mouse(QEvent::MouseButtonRelease, press + QPointF{0, -45},
                        Qt::LeftButton, Qt::NoButton);
          context.process();
          ASSERT_FALSE(session.extrude().dragging());
          ASSERT_TRUE(session.extrude().preview_active());
        }
      }
      context.process();
      ASSERT_TRUE(editor.tools().active_settings().preview_active);
      ASSERT_TRUE(editor.tools().active_settings().can_apply);
      EXPECT_EQ(session.history().undo_count(), 0u);
      const auto preview = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(preview.has_value());
      if (completion == Qt::Key_F18) {
        const std::string_view target = kind == Preview::Bevel
            ? quader::quader_command::kModelingApplyActivePreview.string()
            : "viewport.modal_tool.finalize";
        ASSERT_TRUE(editor.preferences().set_shortcut(target, "F18").ok());
        context.key(Qt::Key_Space);
        ASSERT_TRUE(editor.tools().active_settings().preview_active);
        EXPECT_EQ(session.history().undo_count(), 0u);
      }
      context.key(completion);
      EXPECT_FALSE(editor.tools().active_settings().preview_active);
      const auto completed = editor.query().compiled_object_mesh(created.value());
      ASSERT_TRUE(completed.has_value());
      if (completion == Qt::Key_Escape) {
        EXPECT_EQ(session.history().undo_count(), 0u);
        expect_compiled_mesh_equal(*completed, *source);
      } else {
        EXPECT_EQ(session.history().undo_count(), 1u);
        expect_compiled_mesh_equal(*completed, *preview);
        ASSERT_TRUE(editor.history().undo().ok());
        const auto undone = editor.query().compiled_object_mesh(created.value());
        ASSERT_TRUE(undone.has_value());
        expect_compiled_mesh_equal(*undone, *source);
        ASSERT_TRUE(editor.history().redo().ok());
        const auto redone = editor.query().compiled_object_mesh(created.value());
        ASSERT_TRUE(redone.has_value());
        expect_compiled_mesh_equal(*redone, *preview);
      }
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     NativeCompletionKeysApplyOrCancelSceneToolsThroughTheProductionPanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-native-completion-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const auto tool : {quader::ViewportToolId::LinearArray,
                          quader::ViewportToolId::RadialArray,
                          quader::ViewportToolId::PathArray,
                          quader::ViewportToolId::Pivot,
                          quader::ViewportToolId::Curve,
                          quader::ViewportToolId::CreatePrimitive}) {
    for (const int completion : {Qt::Key_Space, Qt::Key_Escape}) {
      SCOPED_TRACE(static_cast<int>(tool));
      SCOPED_TRACE(completion);
      NavigationPanelTestContext context;
      ASSERT_NE(context.surface, nullptr);
      auto &editor = context.editor;
      auto &session = context.session;
      const auto source = editor.modeling().create_box(
          {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Native completion source");
      ASSERT_TRUE(source.ok()) << source.error_message();
      ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
      if (tool == quader::ViewportToolId::PathArray) {
        const auto path = editor.modeling().create_curve({
            .points = {{.position = {}}, {.position = {12, 0, 0}}},
            .name = "Completion path"});
        ASSERT_TRUE(path.ok()) << path.error_message();
        ASSERT_TRUE(editor.selection().select_object(path.value()).ok());
        ASSERT_TRUE(session.curve().refresh().ok);
      }
      const auto baseline_entities = session.scene().entities();
      const auto baseline_count = editor.query().modeling_objects().size();
      const auto baseline_transform = session.scene().world_transform_matrix(
          source.value().entity);
      session.history().clear();
      context.process();
      ASSERT_TRUE(editor.tools().activate(tool).ok());
      std::size_t added_objects = 0;
      switch (tool) {
      case quader::ViewportToolId::LinearArray:
        ASSERT_TRUE(editor.tools().set_setting(
            "linear_array", "linear_array.count", 3.0).ok());
        added_objects = 2;
        break;
      case quader::ViewportToolId::RadialArray:
        ASSERT_TRUE(session.radial_array().set_copies(3).ok);
        ASSERT_TRUE(session.radial_array().update_center_candidate(
            {3, 0, 0}, {0, 1, 0}, 1.0).ok);
        ASSERT_TRUE(session.radial_array().lock_center().ok);
        added_objects = 2;
        break;
      case quader::ViewportToolId::PathArray:
        ASSERT_TRUE(session.path_array().set_count(3).ok);
        ASSERT_TRUE(session.path_array().select_source(source.value().entity).ok);
        ASSERT_TRUE(session.path_array().preview_valid());
        added_objects = 2;
        break;
      case quader::ViewportToolId::Pivot:
        ASSERT_TRUE(editor.tools().execute_action(
            "pivot", "pivot.align.top_center").ok());
        break;
      case quader::ViewportToolId::Curve:
        for (const quader::QVec3 point : {quader::QVec3{-2, 0, 0},
                                          quader::QVec3{2, 0, 0}}) {
          ASSERT_TRUE(session.curve().begin_point(point).ok);
          ASSERT_TRUE(session.curve().commit_drag().ok);
        }
        ASSERT_TRUE(session.curve().can_apply());
        added_objects = 1;
        break;
      case quader::ViewportToolId::CreatePrimitive: {
        quader::ModelingBoxPreview preview;
        preview.ok = preview.valid = true;
        preview.drag_state = quader::ModelingBoxDragState::Height;
        preview.corners = {quader::QVec3{-2, -2, -2}, quader::QVec3{-2, -2, 2},
                           quader::QVec3{2, -2, 2}, quader::QVec3{2, -2, -2},
                           quader::QVec3{-2, 2, -2}, quader::QVec3{-2, 2, 2},
                           quader::QVec3{2, 2, 2}, quader::QVec3{2, 2, -2}};
        preview.corner_count = 8;
        ASSERT_TRUE(session.primitive_creation().accept_preview(
            preview, "Completion primitive").ok);
        added_objects = 1;
        break;
      }
      default:
        FAIL() << "Missing completion fixture";
      }
      context.process();
      ASSERT_TRUE(editor.tools().active_settings().preview_active);
      ASSERT_TRUE(editor.tools().active_settings().can_apply);
      EXPECT_EQ(session.history().undo_count(), 0u);
      context.key(completion);
      EXPECT_FALSE(editor.tools().active_settings().preview_active);
      if (completion == Qt::Key_Escape) {
        EXPECT_EQ(session.history().undo_count(), 0u);
        EXPECT_EQ(session.scene().entities(), baseline_entities);
        EXPECT_EQ(editor.query().modeling_objects().size(), baseline_count);
        EXPECT_EQ(session.scene().world_transform_matrix(source.value().entity).values,
                  baseline_transform.values);
      } else {
        EXPECT_EQ(session.history().undo_count(), 1u);
        EXPECT_EQ(editor.query().modeling_objects().size(),
                  baseline_count + added_objects);
        if (tool == quader::ViewportToolId::Pivot) {
          const auto moved = editor.query().entity(source.value().entity);
          ASSERT_TRUE(moved.has_value());
          EXPECT_FLOAT_EQ(moved->transform.position.y, 1.0F);
        }
        ASSERT_TRUE(editor.history().undo().ok());
        EXPECT_EQ(session.scene().entities(), baseline_entities);
        EXPECT_EQ(editor.query().modeling_objects().size(), baseline_count);
        EXPECT_EQ(session.scene().world_transform_matrix(source.value().entity).values,
                  baseline_transform.values);
        ASSERT_TRUE(editor.history().redo().ok());
        EXPECT_EQ(editor.query().modeling_objects().size(),
                  baseline_count + added_objects);
      }
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     ProductionPanelSelectionCursorTracksRingModifiersWithoutPointerMotion) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-selection-modifier-cursor-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(panel, *surface, {800, 600});
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  const auto process_input = [&] {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  const auto key = [&](QEvent::Type type, int code,
                       Qt::KeyboardModifiers previous_modifiers) {
    QKeyEvent event(type, code, previous_modifiers);
    send_visible_viewport_event(surface, &event);
    process_input();
  };
  const auto expect_selection_cursor =
      [&](quader::QtApplicationCursorVariant variant) {
        ASSERT_EQ(surface->cursor().shape(), Qt::BitmapCursor);
        EXPECT_EQ(surface->cursor().pixmap().toImage(),
                  quader::qt_application_cursor_pixmap(
                      variant, std::max<qreal>(1.0, surface->devicePixelRatio()))
                      .toImage());
      };
  using Variant = quader::QtApplicationCursorVariant;
  process_input();
  expect_selection_cursor(Variant::Default);

  // Exercise both modifier orders with the pre-transition masks Qt can send.
  key(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
  expect_selection_cursor(Variant::SelectionSubtract);
  key(QEvent::KeyPress, Qt::Key_Alt, Qt::ControlModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  const auto ring_revision = surface->pointer_cursor_presentation_revision();
  for (int idle = 0; idle < 4; ++idle) {
    process_input();
    EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
    EXPECT_EQ(surface->pointer_cursor_presentation_revision(), ring_revision);
  }
  key(QEvent::KeyRelease, Qt::Key_Alt,
      Qt::ControlModifier | Qt::AltModifier);
  expect_selection_cursor(Variant::SelectionSubtract);
  key(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
  expect_selection_cursor(Variant::Default);
  key(QEvent::KeyPress, Qt::Key_Alt, Qt::NoModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  key(QEvent::KeyPress, Qt::Key_Control, Qt::AltModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  key(QEvent::KeyRelease, Qt::Key_Control,
      Qt::ControlModifier | Qt::AltModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  key(QEvent::KeyRelease, Qt::Key_Alt, Qt::AltModifier);
  expect_selection_cursor(Variant::Default);

  key(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
  expect_selection_cursor(Variant::SelectionAdd);
  key(QEvent::KeyPress, Qt::Key_Control, Qt::ShiftModifier);
  expect_selection_cursor(Variant::SelectionSubtract);
  key(QEvent::KeyPress, Qt::Key_Alt,
      Qt::ControlModifier | Qt::ShiftModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  key(QEvent::KeyRelease, Qt::Key_Shift,
      Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);

  // A held ring click retains its traversal cursor after the modifiers lift.
  QMouseEvent press(QEvent::MouseButtonPress, local, global,
                    Qt::LeftButton, Qt::LeftButton,
                    Qt::ControlModifier | Qt::AltModifier);
  send_visible_viewport_event(surface, &press);
  process_input();
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  key(QEvent::KeyRelease, Qt::Key_Alt,
      Qt::ControlModifier | Qt::AltModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  key(QEvent::KeyRelease, Qt::Key_Control, Qt::ControlModifier);
  EXPECT_EQ(surface->cursor().shape(), Qt::ArrowCursor);
  QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &release);
  process_input();
  process_input();
  expect_selection_cursor(Variant::Default);
}

TEST(QtViewportSurfaceInputTests,
     ModifierOnlyKeysUpdateStationaryViewportInput) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-stationary-modifier-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  surface.resize(800, 600);
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
  surface.resize(800, 600);
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
  surface.resize(800, 600);
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
  surface.resize(800, 600);
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
  surface.resize(800, 600);
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
  resize_viewport_surface(panel, *surface, {800, 600});
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
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
    send_visible_viewport_event(surface, &event);
  };
  const auto send_ctrl = [&](bool down) {
    QKeyEvent event(down ? QEvent::KeyPress : QEvent::KeyRelease,
                    Qt::Key_Control, down ? Qt::ControlModifier : Qt::NoModifier);
    send_visible_viewport_event(surface, &event);
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
  send_visible_viewport_event(surface, &sequence_finish);
  send_visible_viewport_event(surface, &sequence_release);
  process_input();
  EXPECT_NE(editor.tools().live_primitive_cube()->flip_faces,
            before_sequence->flip_faces);

  // A keyboard command in the same frame must leave an unrelated click usable.
  ASSERT_TRUE(editor.commands().reset_shortcut(kFlip).ok());
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kBoxSubdivisionsXSetting, 2.0).ok());
  const auto before_click = editor.tools().live_primitive_cube();
  ASSERT_TRUE(before_click);
  QKeyEvent flip_press(QEvent::KeyPress, Qt::Key_F, Qt::ShiftModifier);
  QKeyEvent flip_release(QEvent::KeyRelease, Qt::Key_F, Qt::ShiftModifier);
  send_visible_viewport_event(surface, &flip_press);
  send_visible_viewport_event(surface, &flip_release);
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
    send_visible_viewport_event(surface, &hover_height);
    process_input();
    ASSERT_FALSE(dimension_label->isHidden());
    EXPECT_EQ(dimension_label->text(), QStringLiteral("Height: 32"));
  }
  const QPointF width_button = control_point(6);
  QMouseEvent hover_width(QEvent::MouseMove, width_button, width_button + QPointF{100, 100},
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &hover_width);
  process_input();
  ASSERT_FALSE(dimension_label->isHidden());
  EXPECT_EQ(dimension_label->text(), QStringLiteral("Width: 68.25"));
  const auto feedback_updates = panel.primitive_feedback_update_count();
  for (int frame = 0; frame < 20; ++frame) process_input();
  EXPECT_EQ(panel.primitive_feedback_update_count(), feedback_updates);

  // Shift+F during a held resize keeps its owner and allows subsequent movement.
  const auto before_resize = editor.tools().live_primitive_cube();
  ASSERT_TRUE(before_resize);
  const QPointF dot = control_point(0);
  QMouseEvent hover_handle(QEvent::MouseMove, dot, dot + QPointF{100, 100},
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &hover_handle);
  process_input();
  EXPECT_EQ(surface->cursor().shape(), Qt::OpenHandCursor);
  EXPECT_EQ(dimension_label->text(), QStringLiteral("Width: 68.25"));
  send_mouse(QEvent::MouseButtonPress, dot, Qt::NoModifier);
  process_input();
  ASSERT_TRUE(editor.tools().live_primitive_cube());
  ASSERT_TRUE(editor.tools().live_primitive_cube()->resizing);
  EXPECT_EQ(surface->cursor().shape(), Qt::OpenHandCursor);
  QKeyEvent drag_flip_press(QEvent::KeyPress, Qt::Key_F, Qt::ShiftModifier);
  QKeyEvent drag_flip_release(QEvent::KeyRelease, Qt::Key_F,
                              Qt::ShiftModifier);
  send_visible_viewport_event(surface, &drag_flip_press);
  send_visible_viewport_event(surface, &drag_flip_release);
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
  resize_viewport_surface(panel, *surface, {800, 600});
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
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
    send_visible_viewport_event(surface, &event);
  };
  const auto key = [&](int code,
                       Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QKeyEvent press(QEvent::KeyPress, code, modifiers);
    QKeyEvent release(QEvent::KeyRelease, code, modifiers);
    send_visible_viewport_event(surface, &press);
    send_visible_viewport_event(surface, &release);
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
  key(Qt::Key_F, Qt::ShiftModifier);
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
    return std::array<int, 6>{state.side_count, state.radial_segments, state.rings,
                             state.major_segments, state.minor_segments,
                             state.sphere_subdivision};
  };
  for (const auto [kind, sphere_type] : {
           std::pair{quader::PrimitiveCreationKind::Cylinder, quader::QuaderSphereType::Uv},
           std::pair{quader::PrimitiveCreationKind::Cone, quader::QuaderSphereType::Uv},
           std::pair{quader::PrimitiveCreationKind::Sphere, quader::QuaderSphereType::Uv},
           std::pair{quader::PrimitiveCreationKind::Sphere, quader::QuaderSphereType::Quad},
           std::pair{quader::PrimitiveCreationKind::Torus, quader::QuaderSphereType::Uv}}) {
    SCOPED_TRACE(static_cast<int>(kind));
    SCOPED_TRACE(static_cast<int>(sphere_type));
    ASSERT_TRUE(localization.select_locale("en-US"));
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
    ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
        quader::kPrimitiveTypeSetting, static_cast<double>(kind)).ok());
    const bool quad_sphere = sphere_type == quader::QuaderSphereType::Quad;
    if (quad_sphere) {
      ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
          quader::kSphereTypeSetting, 1.0).ok());
    }
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
    resize_viewport_surface(panel, *surface, {800, 600});
    auto *label = panel.findChild<QLabel *>(QStringLiteral("viewportPrimitiveFeedback_perspective"));
    ASSERT_NE(label, nullptr);
    ASSERT_TRUE(label->isHidden());
    QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &enter);
    send_visible_viewport_event(surface, &focus);
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
      send_visible_viewport_event(surface, &event);
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
    const int button_count = side_count_kind || quad_sphere ? 1 : 2;
    if (quad_sphere) EXPECT_FALSE(project().controls[7].visible);
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
      if (quad_sphere) EXPECT_EQ(label->text(), QStringLiteral("Subdivision: 3"));
      const auto update_count = panel.primitive_feedback_update_count();
      for (int frame = 0; frame < 60; ++frame) process();
      EXPECT_EQ(panel.primitive_feedback_update_count(), update_count)
          << "Unchanged input must not reformat or reset the retained feedback";

      const auto before = editor.tools().live_primitive_creation();
      ASSERT_TRUE(before);
      const auto before_counts = counts(*before);
      const int setting_index = quad_sphere ? 5 : side_count_kind ? 0
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
    send_visible_viewport_event(surface, &leave);
    EXPECT_TRUE(label->isHidden());
    process();
    EXPECT_TRUE(label->isHidden());
    send_visible_viewport_event(surface, &enter);
    mouse(QEvent::MouseMove, project().controls[6].screen_position);
    process();
    ASSERT_FALSE(label->isHidden());
    QFocusEvent blur(QEvent::FocusOut);
    send_visible_viewport_event(surface, &blur);
    EXPECT_TRUE(label->isHidden());
    process();
    EXPECT_FALSE(label->isHidden());
    send_visible_viewport_event(surface, &focus);
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
     InsertEdgeLoopCancelMappingExitsIdleHoverAndDragWithoutChangingHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-loop-cancel-phase-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  for (const int phase : {0, 1, 2}) {
    for (const int mapping : {0, 1, 2}) {
      SCOPED_TRACE(phase);
      SCOPED_TRACE(mapping);
      NavigationPanelTestContext context;
      ASSERT_NE(context.surface, nullptr);
      auto &editor = context.editor;
      auto &modeling = context.session.modeling();
      if (mapping == 1) {
        ASSERT_TRUE(editor.commands().set_shortcut(
            "viewport.cancel_active_tool", "F18").ok());
      } else if (mapping == 2) {
        ASSERT_TRUE(editor.commands().remove_shortcut("viewport.cancel_active_tool").ok());
      }
      const auto box = editor.modeling().create_box(
          {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Cancel phase loop box");
      ASSERT_TRUE(box.ok()) << box.error_message();
      ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
      ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Back).ok());
      context.camera.focus_origin(5);
      ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::InsertEdgeLoop).ok());
      context.session.history().clear();
      ASSERT_TRUE(editor.scene().rename(box.value().entity, "Redo cancel phase loop box").ok());
      ASSERT_TRUE(editor.history().undo().ok());
      ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::InsertEdgeLoop).ok());
      const auto document_before =
          *context.session.assets().mesh(box.value().mesh)->authored_polygon_document;
      const auto selected_before = editor.selection().summary();
      const auto history_before = editor.history().snapshot();
      context.process();
      const auto projected = context.camera.project_world_point({1, 0, 1});
      ASSERT_TRUE(projected.visible);
      const QPointF local{projected.position.x / context.surface->devicePixelRatio(),
                          projected.position.y / context.surface->devicePixelRatio()};
      if (phase != 0) {
        context.mouse(QEvent::MouseMove, local, Qt::NoButton, Qt::NoButton);
        context.process();
        ASSERT_TRUE(modeling.tool_status().insert_edge_loop_has_preview);
      }
      if (phase == 2) {
        context.mouse(QEvent::MouseButtonPress, local, Qt::LeftButton, Qt::LeftButton);
        // Seed a pending owner drag while retaining the physical held button.
        (void)context.surface->take_input_frame(1.0f / 60.0f, false);
        const quader::ModelingToolRayEvent event{
            .ray = {.origin = {1, 0, 5}, .direction = {0, 0, -1}},
            .grid_size = 1.0,
            .edge_radius = 0.2,
            .snap_enabled = false};
        ASSERT_TRUE(modeling.begin_insert_edge_loop_drag(event).ok);
        ASSERT_TRUE(modeling.tool_status().insert_edge_loop_dragging);
      }
      context.key(Qt::Key_Escape);
      if (mapping != 0) {
        EXPECT_EQ(editor.tools().active_tool(), "insert_edge_loop");
        context.key(Qt::Key_F18);
      }
      if (mapping == 2) {
        EXPECT_EQ(editor.tools().active_tool(), "insert_edge_loop");
        ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
      } else {
        EXPECT_EQ(context.panel.tool_state().active_tool, quader::ViewportToolId::Select);
        EXPECT_EQ(editor.tools().active_tool(), "select");
        EXPECT_EQ(editor.viewports().selected_tool(), quader::ViewportToolId::Select);
        EXPECT_EQ(modeling.tool_status().active_tool, quader::ModelingViewportTool::Select);
        EXPECT_FALSE(modeling.tool_status().insert_edge_loop_has_preview);
        EXPECT_FALSE(modeling.tool_status().insert_edge_loop_dragging);
        context.process();
        EXPECT_EQ(context.panel.tool_state().active_tool, quader::ViewportToolId::Select);
        EXPECT_EQ(editor.viewports().selected_tool(), quader::ViewportToolId::Select);
      }
      if (phase == 2) {
        context.mouse(QEvent::MouseButtonRelease, local, Qt::LeftButton, Qt::NoButton);
        context.process();
      }
      const auto &after = *context.session.assets().mesh(box.value().mesh)->authored_polygon_document;
      EXPECT_TRUE(quader_poly::has_same_geometry(after, document_before));
      EXPECT_EQ(editor.selection().summary().mode, quader::QuaderSelectionMode::Edge);
      EXPECT_EQ(editor.selection().summary().selected_edge_count,
                selected_before.selected_edge_count);
      EXPECT_EQ(editor.selection().summary().content_revision, selected_before.content_revision);
      EXPECT_EQ(context.session.history().undo_count(), 0u);
      EXPECT_EQ(context.session.history().redo_count(), 1u);
      expect_history_unchanged(editor.history().snapshot(), history_before);
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     InsertEdgeLoopEscapeInNonOwningPanePreservesOtherPaneOperation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-loop-pane-cancel-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const bool editor_focus : {false, true}) {
    for (const int pending : {0, 1, 2, 3}) {
      SCOPED_TRACE(editor_focus);
      SCOPED_TRACE(pending);
      NavigationPanelTestContext context;
      auto &editor = context.editor;
      ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::TwoHorizontal).ok());
      context.panel.refresh_workspace_layout();
      const auto box = editor.modeling().create_box(
          {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Other pane knife box");
      ASSERT_TRUE(box.ok());
      ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
      ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Knife).ok());
      if (pending == 1) {
        const quader::ModelingToolRayEvent event{
            .ray = {.origin = {-1, -1, 5}, .direction = {0, 0, -1}},
            .camera = {.orthographic = true, .origin = {-1, -1, 5}},
            .snap_enabled = false};
        ASSERT_TRUE(context.session.modeling().accept_knife_point(event).ok);
        ASSERT_TRUE(context.session.modeling().tool_status().knife_has_active_stroke);
      } else if (pending == 2) {
        ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
        ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());
        ASSERT_TRUE(editor.tools().set_setting("extrude", "extrude.amount", 2.15).ok());
        ASSERT_TRUE(context.session.extrude().preview_active());
      } else if (pending == 3) {
        ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
        ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
        ASSERT_TRUE(editor.commands().execute(
            editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
        ASSERT_TRUE(context.session.modeling().operation_preview_active());
      } else {
        ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
      }
      context.session.history().clear();
      const auto document_before =
          *context.session.assets().mesh(box.value().mesh)->authored_polygon_document;
      const auto origin_before = editor.tools().active_settings_presentation_origin();
      const auto history_before = editor.history().snapshot();
      const auto tool_before = editor.tools().active_tool();
      const auto preview_before = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(preview_before.has_value());
      constexpr auto kPaneId = quader::ViewportPaneId::Top;
      ASSERT_TRUE(editor.viewports().record_selected_tool(
          kPaneId, quader::ViewportToolId::InsertEdgeLoop).ok());
      QEvent leave(QEvent::Leave);
      QFocusEvent blur(QEvent::FocusOut);
      send_visible_viewport_event(context.surface, &leave);
      send_visible_viewport_event(context.surface, &blur);
      context.surface = context.panel.surface(kPaneId);
      ASSERT_NE(context.surface, nullptr);
      resize_viewport_surface(context.panel, *context.surface, {800, 600});
      ASSERT_TRUE(editor.viewports().focus_pane(kPaneId).ok());
      ASSERT_FALSE(editor.tools().pane_owns_executing_tool(kPaneId));
      QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
      QFocusEvent focus(QEvent::FocusIn);
      send_visible_viewport_event(context.surface, &enter);
      send_visible_viewport_event(context.surface, &focus);
      context.process();
      if (editor_focus) {
        QWidget panel;
        panel.setFocusPolicy(Qt::StrongFocus);
        panel.show();
        panel.activateWindow();
        panel.setFocus(Qt::OtherFocusReason);
        QCoreApplication::processEvents();
        quader::QtCommandEventFilter filter(editor, [&] { return context.panel.tool_state(); });
        filter.set_input_context_provider([&] {
          return quader::QuaderViewportKeybindingContext{
              .modal_tool_active = context.session.modeling().modal_tool_owns_viewport(),
              .operation_preview_active = context.session.modeling().operation_preview_active()};
        });
        application.installEventFilter(&filter);
        QKeyEvent key_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QKeyEvent key_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&panel, &key_press);
        QApplication::sendEvent(&panel, &key_release);
        application.removeEventFilter(&filter);
      } else {
        context.key(Qt::Key_Escape);
        EXPECT_EQ(context.panel.tool_state().active_tool, quader::ViewportToolId::Select);
      }
      EXPECT_EQ(editor.viewports().selected_tool(kPaneId), quader::ViewportToolId::Select);
      EXPECT_EQ(editor.tools().active_tool(), tool_before);
      EXPECT_EQ(context.session.modeling().tool_status().knife_has_active_stroke, pending == 1);
      EXPECT_EQ(context.session.extrude().preview_active(), pending == 2);
      EXPECT_EQ(context.session.modeling().operation_preview_active(), pending == 3);
      EXPECT_EQ(editor.tools().active_settings_presentation_origin(), origin_before);
      EXPECT_TRUE(quader_poly::has_same_geometry(
          *context.session.assets().mesh(box.value().mesh)->authored_polygon_document,
          document_before));
      const auto preview_after = editor.query().compiled_object_mesh(box.value());
      ASSERT_TRUE(preview_after.has_value());
      EXPECT_EQ(preview_after->indices, preview_before->indices);
      ASSERT_EQ(preview_after->vertices.size(), preview_before->vertices.size());
      for (std::size_t index = 0; index < preview_before->vertices.size(); ++index) {
        EXPECT_EQ(preview_after->vertices[index].position.x, preview_before->vertices[index].position.x);
        EXPECT_EQ(preview_after->vertices[index].position.y, preview_before->vertices[index].position.y);
        EXPECT_EQ(preview_after->vertices[index].position.z, preview_before->vertices[index].position.z);
      }
      expect_history_unchanged(editor.history().snapshot(), history_before);
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     MarkingMenuTapSelectsWithoutPopupForCoalescedAndRemappedKeys) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-menu-tap-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const int mapping : {0, 1, 2}) {
    const bool remapped = mapping != 0;
    for (const bool coalesced : {false, true}) {
      SCOPED_TRACE(mapping);
      SCOPED_TRACE(coalesced);
      NavigationPanelTestContext context;
      ASSERT_NE(context.surface, nullptr);
      ASSERT_TRUE(context.editor.viewports().set_layout(
          quader::ViewportLayoutId::FourQuad).ok());
      context.panel.refresh_workspace_layout();
      ASSERT_TRUE(context.editor.tools().activate(
          quader::ViewportToolId::Move).ok());
      if (remapped) {
        ASSERT_TRUE(context.editor.preferences().set_shortcut(
            "viewport.marking_menu", mapping == 1
                ? "Ctrl+Alt+F20" : "Ctrl+K Alt+F20").ok());
        context.key(Qt::Key_Q);
        EXPECT_EQ(context.editor.tools().active_tool(), "move");
      }
      context.process();
      const auto history_before = context.editor.history().snapshot();
      const int key = remapped ? Qt::Key_F20 : Qt::Key_Q;
      const Qt::KeyboardModifiers modifiers = mapping == 1
          ? Qt::ControlModifier | Qt::AltModifier
          : (mapping == 2 ? Qt::AltModifier : Qt::NoModifier);
      QTimer unexpected_popup;
      unexpected_popup.setSingleShot(true);
      QObject::connect(&unexpected_popup, &QTimer::timeout,
                       &unexpected_popup, [] {
        ADD_FAILURE() << "A tap constructed the marking popup before its hold.";
        if (QWidget *popup = QApplication::activePopupWidget()) popup->close();
      });
      const auto process_without_popup = [&] {
        unexpected_popup.start(1000);
        context.process();
        unexpected_popup.stop();
        EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
      };
      if (mapping == 2) {
        QKeyEvent prefix(QEvent::KeyPress, Qt::Key_K, Qt::ControlModifier);
        QKeyEvent prefix_release(QEvent::KeyRelease, Qt::Key_K, Qt::NoModifier);
        send_visible_viewport_event(context.surface, &prefix);
        send_visible_viewport_event(context.surface, &prefix_release);
        if (!coalesced) process_without_popup();
      }
      QKeyEvent press(QEvent::KeyPress, key, modifiers);
      send_visible_viewport_event(context.surface, &press);
      if (!coalesced) {
        process_without_popup();
        EXPECT_EQ(context.editor.tools().active_tool(), "move");
        QKeyEvent repeat(QEvent::KeyPress, key, modifiers, {}, true);
        QKeyEvent repeated_release(QEvent::KeyRelease, key, modifiers, {}, true);
        send_visible_viewport_event(context.surface, &repeat);
        send_visible_viewport_event(context.surface, &repeated_release);
        process_without_popup();
        EXPECT_EQ(context.editor.tools().active_tool(), "move");
      }
      // Releasing a modifier first does not change the accepted launcher's key.
      QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier);
      send_visible_viewport_event(context.surface, &release);
      process_without_popup();
      EXPECT_EQ(context.editor.tools().active_tool(), "select");
      for (const auto pane_id : {quader::ViewportPaneId::Perspective,
                                quader::ViewportPaneId::Top,
                                quader::ViewportPaneId::Front,
                                quader::ViewportPaneId::Side}) {
        EXPECT_EQ(context.editor.viewports().selected_tool(pane_id),
                  quader::ViewportToolId::Select);
      }
      expect_history_unchanged(context.editor.history().snapshot(), history_before);
      process_without_popup();
      if (remapped) {
        ASSERT_TRUE(context.editor.preferences().set_shortcut(
            quader::quader_command::kToolsActivateSelect, "Q").ok());
        ASSERT_TRUE(context.editor.tools().activate(
            quader::ViewportToolId::Move).ok());
        context.key(Qt::Key_Q);
        EXPECT_EQ(context.editor.tools().active_tool(), "select");
        EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
      }
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     MarkingMenuTapAcceptsPendingExtrusionThroughTheNormalToolLifecycle) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-menu-tap-settlement-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  const auto box = context.editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}});
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(context.editor.selection().select_face(box.value(), 1).ok());
  context.session.history().clear();
  ASSERT_TRUE(context.editor.tools().activate(quader::ViewportToolId::Extrude).ok());
  ASSERT_TRUE(context.editor.tools().set_setting(
      "extrude", "extrude.amount", 2.15).ok());
  ASSERT_TRUE(context.session.extrude().preview_active());
  const auto preview = context.editor.query().compiled_object_mesh(box.value());
  ASSERT_TRUE(preview.has_value());
  context.process();
  context.key(Qt::Key_Q);
  EXPECT_EQ(context.editor.tools().active_tool(), "select");
  EXPECT_FALSE(context.session.extrude().preview_active());
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_EQ(context.session.history().undo_count(), 1u);
  const auto accepted = context.editor.query().compiled_object_mesh(box.value());
  ASSERT_TRUE(accepted.has_value());
  expect_compiled_mesh_equal(*accepted, *preview);
  ASSERT_TRUE(context.editor.history().undo().ok());
  ASSERT_TRUE(context.editor.history().redo().ok());
  const auto replayed = context.editor.query().compiled_object_mesh(box.value());
  ASSERT_TRUE(replayed.has_value());
  expect_compiled_mesh_equal(*replayed, *preview);
}

TEST(QtViewportSurfaceInputTests,
     MarkingMenuHoldOpensOnlyAfterThresholdAndConsumesItsRelease) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-menu-hold-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(std::filesystem::path(QUADER_SOURCE_DIR) /
                                    "resources" / "locales", "en-US"));
  context.panel.set_localization(localization);
  ASSERT_TRUE(context.editor.tools().activate(quader::ViewportToolId::Move).ok());
  context.process();
  QTimer watchdog;
  watchdog.setSingleShot(true);
  QObject::connect(&watchdog, &QTimer::timeout, &watchdog, [] {
    ADD_FAILURE() << "The marking gesture opened early or failed to consume release.";
    if (QWidget *popup = QApplication::activePopupWidget()) popup->close();
  });
  QKeyEvent press(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier);
  send_visible_viewport_event(context.surface, &press);
  watchdog.start(1000);
  context.process();
  watchdog.stop();
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_EQ(context.editor.tools().active_tool(), "move");
  // Event-loop time, rather than synthesized frame deltas, defines the hold.
  QEventLoop before_threshold;
  QTimer::singleShot(80, &before_threshold, &QEventLoop::quit);
  before_threshold.exec();
  watchdog.start(1000);
  context.process();
  watchdog.stop();
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_EQ(context.editor.tools().active_tool(), "move");
  QEventLoop hold;
  QTimer::singleShot(150, &hold, &QEventLoop::quit);
  hold.exec();
  bool popup_opened = false;
  QTimer interaction;
  interaction.setSingleShot(true);
  QObject::connect(&interaction, &QTimer::timeout, &interaction, [&] {
    QWidget *popup = QApplication::activePopupWidget();
    if (popup == nullptr) return;
    popup_opened = popup->objectName() == QStringLiteral("viewport_marking_menu");
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Q, Qt::NoModifier);
    QApplication::sendEvent(popup, &release);
  });
  interaction.start(0);
  watchdog.start(1000);
  context.process();
  interaction.stop();
  watchdog.stop();
  EXPECT_TRUE(popup_opened);
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  EXPECT_EQ(context.editor.tools().active_tool(), "move");
  QKeyEvent trailing_release(QEvent::KeyRelease, Qt::Key_Q, Qt::NoModifier);
  send_visible_viewport_event(context.surface, &trailing_release);
  context.process();
  EXPECT_EQ(context.editor.tools().active_tool(), "move");
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
}

TEST(QtViewportSurfaceInputTests,
     MarkingMenuPendingHoldAbandonsOnFocusRuntimeAndOtherInput) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-marking-menu-cancel-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  for (const int interruption : {0, 1, 2, 3}) {
    SCOPED_TRACE(interruption);
    NavigationPanelTestContext context;
    ASSERT_NE(context.surface, nullptr);
    ASSERT_TRUE(context.editor.tools().activate(quader::ViewportToolId::Scale).ok());
    context.process();
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Q, Qt::NoModifier);
    send_visible_viewport_event(context.surface, &press);
    context.process();
    if (interruption == 0) {
      QFocusEvent lost_focus(QEvent::FocusOut);
      send_visible_viewport_event(context.surface, &lost_focus);
      context.process();
      QFocusEvent focus(QEvent::FocusIn);
      send_visible_viewport_event(context.surface, &focus);
    } else if (interruption == 1) {
      context.panel.abandon_interactions();
      context.panel.attach_workspace(context.session.viewport_workspace());
    } else if (interruption == 2) {
      context.key(Qt::Key_W);
      EXPECT_EQ(context.editor.tools().active_tool(), "move");
    } else {
      context.mouse(QEvent::MouseButtonPress, {400, 300},
                    Qt::MiddleButton, Qt::MiddleButton);
      context.process();
    }
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Q, Qt::NoModifier);
    send_visible_viewport_event(context.surface, &release);
    context.process();
    EXPECT_EQ(context.editor.tools().active_tool(),
              interruption == 2 ? "move" : "scale");
    EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
    if (interruption == 3) {
      context.mouse(QEvent::MouseButtonRelease, {400, 300},
                    Qt::MiddleButton, Qt::NoButton);
      context.process();
    }
    context.process();
    EXPECT_EQ(context.editor.tools().active_tool(),
              interruption == 2 ? "move" : "scale");
  }
}

TEST(QtViewportSurfaceInputTests,
     ZPressReleaseTogglesFocusedPaneBetweenShadedAndWireframeWithoutPopup) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-wireframe-shaded-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  ASSERT_TRUE(context.editor.viewports().set_layout(quader::ViewportLayoutId::TwoHorizontal).ok());
  context.panel.refresh_workspace_layout();
  ASSERT_TRUE(context.editor.viewports().set_shading_mode(
      quader::ViewportPaneId::Perspective, quader::QuaderShadingMode::Shaded).ok());
  ASSERT_TRUE(context.editor.viewports().set_shading_mode(
      quader::ViewportPaneId::Top, quader::QuaderShadingMode::Shaded).ok());
  context.process();
  context.key(Qt::Key_Z);
  EXPECT_EQ(context.editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
            quader::QuaderShadingMode::Wireframe);
  EXPECT_EQ(context.editor.viewports().shading_mode(quader::ViewportPaneId::Top),
            quader::QuaderShadingMode::Shaded);
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
  context.key(Qt::Key_Z);
  EXPECT_EQ(context.editor.viewports().shading_mode(quader::ViewportPaneId::Perspective),
            quader::QuaderShadingMode::Shaded);
  EXPECT_EQ(context.editor.viewports().shading_mode(quader::ViewportPaneId::Top),
            quader::QuaderShadingMode::Shaded);
  EXPECT_EQ(QApplication::activePopupWidget(), nullptr);
}

TEST(QtViewportSurfaceInputTests,
     InsertEdgeLoopRepeatsPhysicalClicksEscapeSelectsAndInterruptionsRetainTool) {
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
  resize_viewport_surface(panel, *surface, {800, 600});
  auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  camera.focus_origin(5);
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &enter);
  send_visible_viewport_event(surface, &focus);
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
    send_visible_viewport_event(surface, &event);
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
  const auto history_before_interruptions = editor.history().snapshot();
  const auto hover = [&] {
    mouse(QEvent::MouseMove, point_at_height(0.75f));
    process();
    ASSERT_TRUE(session.modeling().tool_status().insert_edge_loop_has_preview);
  };
  hover();
  QFocusEvent blur(QEvent::FocusOut);
  send_visible_viewport_event(surface, &blur);
  process();
  expect_active();
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));
  expect_history_unchanged(editor.history().snapshot(),
                           history_before_interruptions);
  send_visible_viewport_event(surface, &focus);
  hover();
  QEvent leave(QEvent::Leave);
  send_visible_viewport_event(surface, &leave);
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
  EXPECT_EQ(session.history().redo_count(), 0u);
  expect_history_unchanged(editor.history().snapshot(),
                           history_before_interruptions);

  send_visible_viewport_event(surface, &enter);
  send_visible_viewport_event(surface, &focus);
  for (const int delta : {120, -120}) {
    const auto before_size = camera.state().orthographic_size;
    QWheelEvent wheel({400, 300}, {500, 400}, {}, {0, delta}, Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    send_visible_viewport_event(surface, &wheel);
    process();
    if (delta > 0) EXPECT_LT(camera.state().orthographic_size, before_size);
    else EXPECT_GT(camera.state().orthographic_size, before_size);
    expect_active();
    EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));
    EXPECT_EQ(session.history().undo_count(), 3u);
    EXPECT_EQ(session.history().redo_count(), 0u);
    expect_history_unchanged(editor.history().snapshot(),
                             history_before_interruptions);
  }
  hover();
  QKeyEvent escape_press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QKeyEvent escape_release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
  send_visible_viewport_event(surface, &escape_press);
  send_visible_viewport_event(surface, &escape_release);
  process();
  EXPECT_EQ(panel.tool_state().active_tool, quader::ViewportToolId::Select);
  EXPECT_EQ(session.modeling().tool_status().active_tool,
            quader::ModelingViewportTool::Select);
  EXPECT_EQ(session.modeling().selection_mode(),
            quader::ModelingSelectionMode::Edge);
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_has_preview);
  EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_dragging);
  EXPECT_TRUE(quader_poly::has_same_geometry(document(), inserted));
  EXPECT_EQ(session.history().undo_count(), 3u);
  EXPECT_EQ(session.history().redo_count(), 0u);
  expect_history_unchanged(editor.history().snapshot(),
                           history_before_interruptions);
}

TEST(QtViewportSurfaceInputTests,
     StationaryOrthographicHoverRechecksScreenToleranceWhenOnlyZoomChanges) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-stationary-screen-pick-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {0.5f, -2, -1}, .max = {4, 2, 1}}, "Stationary pick input box").ok());
  ASSERT_TRUE(context.editor.tools().activate(quader::ViewportToolId::Select).ok());
  ASSERT_TRUE(context.editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.editor.main_viewport().set_view(quader::ViewportView::Back).ok());
  context.camera.focus_origin(24.0f);
  context.process();
  const auto center = context.surface->render_size_pixels() * 0.5f;
  const auto original_ray = context.camera.ray_from_viewport_point(center);
  const auto distance_pixels = [&] {
    const auto edge = context.camera.project_world_point({0.5f, 0, 1});
    return std::hypot(edge.position.x - center.x, edge.position.y - center.y) /
           context.surface->devicePixelRatio();
  };
  ASSERT_GT(distance_pixels(), 8.0);
  EXPECT_EQ(context.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
  int zoom_steps = 0;
  while (distance_pixels() > 6.0 && zoom_steps < 30) {
    context.camera.scroll(-1.0f);
    ++zoom_steps;
  }
  ASSERT_LE(distance_pixels(), 6.0);
  const auto zoomed_ray = context.camera.ray_from_viewport_point(center);
  EXPECT_NEAR(quader::length(zoomed_ray.origin - original_ray.origin), 0.0f, 0.00001f);
  EXPECT_NEAR(quader::length(zoomed_ray.direction - original_ray.direction), 0.0f, 0.00001f);
  // No mouse event: camera projection alone must invalidate the hover gate.
  context.process();
  EXPECT_EQ(context.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::Edge);
  context.camera.scroll(static_cast<float>(zoom_steps));
  ASSERT_GT(distance_pixels(), 8.0);
  context.process();
  EXPECT_EQ(context.editor.selection().hover_summary().target.kind,
            quader::QuaderActiveSelectionKind::None);
}

TEST(QtViewportSurfaceInputTests,
     ComponentHoverAndClickSharePixelToleranceAcrossZoomAndUiScale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-component-screen-pick-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  for (const auto mode : {quader::QuaderSelectionMode::Vertex,
                          quader::QuaderSelectionMode::Edge}) {
    for (const float distance : {30.0f, 100.0f}) {
      for (const float ui_scale : {1.0f, 2.0f}) {
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(distance);
        SCOPED_TRACE(ui_scale);
        NavigationPanelTestContext context;
        ASSERT_NE(context.surface, nullptr);
        const auto box = context.editor.modeling().create_box(
            {.min = {-4, -4, -4}, .max = {4, 4, 4}}, "Screen pick input box");
        ASSERT_TRUE(box.ok()) << box.error_message();
        ASSERT_TRUE(context.editor.tools().activate(quader::ViewportToolId::Select).ok());
        ASSERT_TRUE(context.editor.selection().set_mode(mode).ok());
        ASSERT_TRUE(context.editor.selection().clear().ok());
        ASSERT_TRUE(context.editor.selection().clear_hover().ok());
        context.panel.apply_ui_scale(quader::QtUiScaleMetrics{ui_scale, 1u});
        resize_viewport_surface(context.panel, *context.surface, {800, 600});
        ASSERT_TRUE(context.camera.restore_perspective_pose(
            {.eye = {0, 0, distance}, .target = {}, .up = {0, 1, 0}}));
        context.process();
        const bool vertex_mode = mode == quader::QuaderSelectionMode::Vertex;
        const auto kind = vertex_mode ? quader::QuaderActiveSelectionKind::Vertex
                                      : quader::QuaderActiveSelectionKind::Edge;
        const auto target = context.camera.project_world_point(
            vertex_mode ? quader::QVec3{4, 4, 4} : quader::QVec3{4, 0, 4});
        ASSERT_TRUE(target.visible);
        const auto local_at = [&](float offset) {
          const qreal dpr = context.surface->devicePixelRatio();
          return QPointF{target.position.x / dpr + offset * ui_scale,
                         target.position.y / dpr -
                             (vertex_mode ? offset * ui_scale : 0.0f)};
        };
        // Both offsets are outside the mesh silhouette: body fallback cannot
        // turn a missed component into an apparently successful selection.
        context.mouse(QEvent::MouseMove, local_at(20.0f), Qt::NoButton, Qt::NoButton);
        context.process();
        EXPECT_EQ(context.editor.selection().hover_summary().target.kind,
                  quader::QuaderActiveSelectionKind::None);
        const QPointF inside = local_at(6.0f);
        context.mouse(QEvent::MouseMove, inside, Qt::NoButton, Qt::NoButton);
        context.process();
        const auto hovered = context.editor.selection().hover_summary().target;
        ASSERT_EQ(hovered.kind, kind);
        ASSERT_EQ(hovered.object_id, box.value().modeling_object_id);
        EXPECT_FALSE(context.editor.selection().summary().has_selection);
        context.mouse(QEvent::MouseButtonPress, inside, Qt::LeftButton, Qt::LeftButton);
        context.mouse(QEvent::MouseButtonRelease, inside, Qt::LeftButton, Qt::NoButton);
        context.process();
        const auto selected = context.editor.selection().summary();
        EXPECT_EQ(selected.active_kind, kind);
        EXPECT_EQ(selected.active_object_id, hovered.object_id);
        if (vertex_mode) {
          EXPECT_EQ(selected.selected_vertex_count, 1u);
          EXPECT_EQ(selected.active_vertex_id, hovered.vertex_id);
        } else {
          EXPECT_EQ(selected.selected_edge_count, 1u);
          EXPECT_EQ(selected.active_edge_a, hovered.edge_a);
          EXPECT_EQ(selected.active_edge_b, hovered.edge_b);
        }
      }
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     PerspectiveEdgeLoopTracksSurfacePixelsAndCtrlSnappingAcrossHeaderScales) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-perspective-edge-loop-input-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  for (const float ui_scale : {1.0f, 2.0f}) {
    for (const bool snap_enabled : {false, true}) {
      SCOPED_TRACE(ui_scale);
      SCOPED_TRACE(snap_enabled);
      quader::SceneSession session;
      quader::QuaderEditorApi editor(
          quader::QuaderApiSessionInternalAccess::from_scene_session(session));
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      const auto box = editor.modeling().create_box(
          {.min = {-1, -1, -5}, .max = {1, 1, 5}}, "Perspective loop input box");
      ASSERT_TRUE(box.ok()) << box.error_message();
      ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
      // Keep the foreshortened seed separate from the transverse end-cap
      // edges, including within the larger 200% picking aperture.
      ASSERT_TRUE(editor.viewports().set_layout(quader::ViewportLayoutId::TwoHorizontal).ok());
      constexpr auto kPaneId = quader::ViewportPaneId::Top;
      ASSERT_TRUE(editor.viewports().set_view(kPaneId, quader::ViewportView::Perspective).ok());
      ASSERT_TRUE(editor.viewports().focus_pane(kPaneId).ok());
      ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::InsertEdgeLoop).ok());
      ASSERT_TRUE(editor.viewports().set_grid_preset(5).ok());
      ASSERT_FLOAT_EQ(quader::viewport_grid_world_size_for_preset(5), 0.5f);

      quader::QtViewportPanel panel;
      panel.resize(1400, 1000);
      panel.attach_workspace(session.viewport_workspace());
      panel.apply_ui_scale(quader::QtUiScaleMetrics{ui_scale, 1u});
      panel.refresh_workspace_layout();
      ASSERT_TRUE(editor.viewports().set_control(
          kPaneId, quader::QuaderViewportControl::SnappingEnabled, true).ok());
      ASSERT_TRUE(editor.viewports().set_control(
          kPaneId, quader::QuaderViewportControl::GridSnap, snap_enabled).ok());
      ASSERT_TRUE(editor.viewports().set_control(
          quader::ViewportPaneId::Perspective,
          quader::QuaderViewportControl::GridSnap, !snap_enabled).ok());
      auto *surface = panel.surface(kPaneId);
      auto *container = panel.findChild<QWidget *>(
          QStringLiteral("viewportSurfaceContainer_top"));
      auto *header = panel.findChild<QWidget *>(QStringLiteral("viewportToolbar_top"));
      ASSERT_NE(surface, nullptr);
      ASSERT_NE(container, nullptr);
      ASSERT_NE(header, nullptr);
      ASSERT_GT(header->height(), 0);
      ASSERT_EQ(container->y(), header->height());
      ASSERT_EQ(container->height(), container->parentWidget()->height() - header->height());
      // The hidden host has no native resize delivery; retain its actual
      // header-excluding content size when feeding the production adapter.
      surface->resize(container->size());
      auto &camera = session.viewport_workspace().camera(kPaneId);
      ASSERT_TRUE(camera.restore_perspective_pose(
          {.eye = {2, 3, 8}, .target = {}, .up = {0, 1, 0}}));
      const QPointF center{surface->width() * 0.5, surface->height() * 0.5};
      QEnterEvent enter(center, center, QPointF(surface->mapToGlobal(center.toPoint())));
      QFocusEvent focus(QEvent::FocusIn);
      send_visible_viewport_event(surface, &enter);
      send_visible_viewport_event(surface, &focus);
      const auto process = [&] {
        panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                            session.native_tools(), &session.point_placement(), 1.0 / 60.0);
      };
      process();
      EXPECT_FLOAT_EQ(camera.viewport_size().x, surface->render_size_pixels().x);
      EXPECT_FLOAT_EQ(camera.viewport_size().y, surface->render_size_pixels().y);
      const auto near_end = camera.project_world_point({1, 1, 5});
      const auto far_end = camera.project_world_point({1, 1, -5});
      const auto target = camera.project_world_point({1, 1, 0.37f});
      ASSERT_TRUE(near_end.visible);
      ASSERT_TRUE(far_end.visible);
      ASSERT_TRUE(target.visible);
      ASSERT_GT(far_end.depth, near_end.depth * 2.0f);
      const auto edge_delta = near_end.position - far_end.position;
      const float edge_length = std::hypot(edge_delta.x, edge_delta.y);
      ASSERT_GT(edge_length, 1.0f);
      const quader::QVec2 perpendicular{-edge_delta.y / edge_length,
                                        edge_delta.x / edge_length};
      const auto mouse = [&](QEvent::Type type, float offset, Qt::MouseButtons held,
                             Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const auto pixels = target.position + perpendicular *
            (offset * ui_scale * static_cast<float>(surface->devicePixelRatio()));
        const QPointF local{pixels.x / surface->devicePixelRatio(),
                            pixels.y / surface->devicePixelRatio()};
        QMouseEvent event(type, local, QPointF(surface->mapToGlobal(local.toPoint())),
                         type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                         held, modifiers);
        send_visible_viewport_event(surface, &event);
      };
      float expected_z = 0.37f;
      const auto expect_preview = [&] {
        ASSERT_TRUE(session.modeling().tool_status().insert_edge_loop_has_preview);
        const auto frame = session.modeling().volatile_overlay_frame();
        std::size_t loop_lines = 0u;
        for (const auto &line : frame.lines) {
          if (std::abs(line.start.z) >= 4.99f || std::abs(line.end.z) >= 4.99f) {
            continue;
          }
          ++loop_lines;
          EXPECT_NEAR(line.start.z, expected_z, 0.0002f);
          EXPECT_NEAR(line.end.z, expected_z, 0.0002f);
        }
        EXPECT_EQ(loop_lines, 4u);
      };
      session.history().clear();
      // Default edge picking allows eight design pixels. Start off the edge
      // and cross it repeatedly without changing the projected loop position.
      for (const float offset : {6.0f, -6.0f, 3.0f, -3.0f, 6.0f}) {
        SCOPED_TRACE(offset);
        mouse(QEvent::MouseMove, offset, Qt::NoButton);
        process();
        expect_preview();
      }
      const auto control = [&](bool held) {
        expected_z = held ? 0.5f : 0.37f;
        QKeyEvent event(held ? QEvent::KeyPress : QEvent::KeyRelease,
                        Qt::Key_Control, held ? Qt::ControlModifier : Qt::NoModifier);
        send_visible_viewport_event(surface, &event);
        process();
        expect_preview();
        EXPECT_EQ(panel.operation_input_context().control, held);
        const auto hints = editor.tools().active_input_hints(panel.operation_input_context());
        const auto snap_hint = std::find_if(hints.begin(), hints.end(), [](const auto &hint) {
          return hint.gesture == quader::QuaderOperationInputGesture::Control;
        });
        ASSERT_NE(snap_hint, hints.end());
        EXPECT_EQ(snap_hint->label_fallback, "Grid Snap");
        EXPECT_EQ(snap_hint->active, held);
      };
      // Modifier edges alone must reproject a stationary cursor's preview.
      control(true);
      control(false);
      ASSERT_TRUE(editor.viewports().set_control(
          kPaneId, quader::QuaderViewportControl::SnappingEnabled, false).ok());
      control(true);
      EXPECT_FALSE(panel.operation_input_context().grid_snap_available);
      control(false);
      EXPECT_EQ(session.history().undo_count(), 0u);
      if (snap_enabled) control(true);
      const auto commit_modifiers = snap_enabled ? Qt::ControlModifier : Qt::NoModifier;
      mouse(QEvent::MouseButtonPress, 6.0f, Qt::LeftButton, commit_modifiers);
      process();
      EXPECT_EQ(session.history().undo_count(), 1u);
      mouse(QEvent::MouseButtonRelease, 6.0f, Qt::NoButton, commit_modifiers);
      process();
      EXPECT_FALSE(session.modeling().tool_status().insert_edge_loop_dragging);
      EXPECT_EQ(session.history().undo_count(), 1u);
      const auto *mesh = session.assets().mesh(box.value().mesh);
      ASSERT_NE(mesh, nullptr);
      ASSERT_NE(mesh->authored_polygon_document, nullptr);
      const auto &vertices = mesh->authored_polygon_document->vertices;
      ASSERT_EQ(vertices.size(), 12u);
      EXPECT_EQ(std::count_if(vertices.begin(), vertices.end(),
                             [&](const auto &vertex) {
                               return std::abs(vertex.position.z - expected_z) < 0.0002f;
                             }), 4);
      EXPECT_EQ(editor.viewports().control(quader::ViewportPaneId::Perspective,
                                           quader::QuaderViewportControl::GridSnap),
                !snap_enabled);
    }
  }
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
    resize_viewport_surface(panel, *surface, {800, 600});
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
      send_visible_viewport_event(surface, &event);
    };
    const auto origin = local_at({});
    QEnterEvent enter(origin, origin, QPointF(surface->mapToGlobal(origin.toPoint())));
    send_visible_viewport_event(surface, &enter);
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
    send_visible_viewport_event(surface, &leave);
    activate.setFocus();
    ASSERT_TRUE(editor.tools().set_setting("radial_array", "radial_array.copies", 4.0).ok());
    frame();
    ASSERT_TRUE(tool.preview_state().active);
    EXPECT_EQ(tool.preview_state().sources.front().generated_world_transforms.size(), 3u);
    EXPECT_FLOAT_EQ(quader::length_squared(tool.center_state().world_position -
                                          candidate.world_position), 0.0f);
    send_visible_viewport_event(surface, &enter);
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &focus);
    frame();
    for (const int delta : {120, -120}) {
      const auto copies_before = tool.copies();
      const auto zoom_before = camera.state().orthographic_size;
      QWheelEvent wheel(origin, QPointF(surface->mapToGlobal(origin.toPoint())),
                        {}, {0, delta}, Qt::NoButton, Qt::ShiftModifier,
                        Qt::NoScrollPhase, false);
      send_visible_viewport_event(surface, &wheel);
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
      send_visible_viewport_event(surface, &wheel);
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
    send_visible_viewport_event(surface, &plain_wheel);
    frame();
    EXPECT_LT(camera.state().orthographic_size, zoom_before_plain_wheel);
    EXPECT_EQ(tool.copies(), 4u);
    EXPECT_EQ(tool.repeats(), 1u);
    EXPECT_TRUE(tool.preview_state().active);
    ASSERT_FALSE(tool.create_instances());
    for (const bool expected_instances : {true, false}) {
      QKeyEvent press(QEvent::KeyPress, Qt::Key_I, Qt::NoModifier);
      QKeyEvent release(QEvent::KeyRelease, Qt::Key_I, Qt::NoModifier);
      send_visible_viewport_event(surface, &press);
      send_visible_viewport_event(surface, &release);
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
  surface.resize(800, 600);
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
     FocusLossRetainsAuthoredCurveDraftAndLeavesSelectedControlsInert) {
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
    const auto undo_steps_before = session.history().undo_count();
    const auto redo_steps_before = session.history().redo_count();
    const auto expect_no_published_history = [&]() {
      EXPECT_EQ(session.history().undo_count(), undo_steps_before);
      EXPECT_EQ(session.history().redo_count(), redo_steps_before);
      if (drawing) {
        // Undo can discard an unfinished draft without publishing a history step.
        EXPECT_TRUE(editor.history().can_undo());
        EXPECT_FALSE(editor.history().can_redo());
      } else {
        expect_history_unchanged(editor.history().snapshot(), history_before);
      }
    };

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
    const auto authored = *curve.preview_state().document;

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
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(authored));
    if (drawing) {
      EXPECT_EQ(curve.control_selection().controls.size(), 1u);
      EXPECT_TRUE(curve.control_selection().active.has_value());
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
    expect_no_published_history();

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
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(authored));
    expect_no_published_history();
  }
}

TEST(QtViewportSurfaceInputTests,
     MaterialRightClickReleaseAndDoubleClickStayDistinctFromDrag) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-material-click-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const Qt::KeyboardModifiers modifiers : {
           Qt::KeyboardModifiers(Qt::ControlModifier),
           Qt::KeyboardModifiers(Qt::ShiftModifier),
       }) {
    SCOPED_TRACE(modifiers == Qt::ControlModifier ? "Ctrl RMB" : "Shift RMB");
    quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
    surface.resize(800, 600);
    const QPointF press_local{24.0, 32.0};
    const QPointF press_global{124.0, 232.0};
    const auto has_right_gesture = [&](const quader::QtViewportInputFrame &frame,
                                      QuaderInputGesture gesture) {
      return std::any_of(
          frame.input.pressed_input_chords.begin(),
          frame.input.pressed_input_chords.end(),
          [&](const QuaderInputChord &chord) {
            return chord.primary == modifiers.testFlag(Qt::ControlModifier) &&
                   chord.shift == modifiers.testFlag(Qt::ShiftModifier) &&
                   !chord.alt &&
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
}

TEST(QtViewportSurfaceInputTests,
     WasdNavigationReleaseDoesNotOpenContextMenuOverSelectedTargets) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-fly-context-menu-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(__FILE__).parent_path().parent_path() /
          "resources" / "locales",
      "en-US"));

  for (const auto mode : {quader::QuaderSelectionMode::Object,
                          quader::QuaderSelectionMode::Vertex}) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    const auto box = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Navigation target");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
    ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    quader::QVec3 target{0, 0, 1};
    if (mode == quader::QuaderSelectionMode::Vertex) {
      const auto *mesh = session.assets().mesh(box.value().mesh);
      ASSERT_NE(mesh, nullptr);
      ASSERT_NE(mesh->authored_polygon_document, nullptr);
      ASSERT_FALSE(mesh->authored_polygon_document->vertices.empty());
      const auto &vertex = mesh->authored_polygon_document->vertices.front();
      target = vertex.position;
      ASSERT_TRUE(editor.selection().select_vertex(box.value(), vertex.id).ok());
    }
    session.history().clear();

    quader::QtViewportPanel panel;
    panel.attach_workspace(session.viewport_workspace());
    panel.set_localization(localization);
    auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
    ASSERT_NE(surface, nullptr);
    resize_viewport_surface(panel, *surface, {800, 600});
    auto &camera = session.viewport_workspace().camera(
        quader::ViewportPaneId::Perspective);
    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = target + quader::normalize(target) * 8.0f,
         .target = target,
         .up = {0, 1, 0}}));
    const QPointF local{400, 300};
    const QPointF global{500, 400};
    QEnterEvent enter(local, local, global);
    send_visible_viewport_event(surface, &enter);
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &focus);
    const auto process_input = [&]() {
      panel.process_input(editor, session.scene(), session.assets(),
                          session.modeling(), {}, session.native_tools(),
                          &session.point_placement(), 1.0 / 600.0);
    };
    const auto right_button = [&](QEvent::Type type) {
      QMouseEvent event(type, local, global, Qt::RightButton,
                       type == QEvent::MouseButtonPress ? Qt::RightButton
                                                       : Qt::NoButton,
                       Qt::NoModifier);
      send_visible_viewport_event(surface, &event);
    };
    const auto movement_key = [&](QEvent::Type type) {
      QKeyEvent event(type, Qt::Key_W, Qt::NoModifier);
      send_visible_viewport_event(surface, &event);
    };
    // Observe the actual presenter and close its nested event loop if a menu
    // opens. A subsequent plain click proves that the presenter is enabled.
    const auto release_and_count_menus = [&]() {
      int opened_menus = 0;
      QTimer popup_observer;
      QObject::connect(&popup_observer, &QTimer::timeout, [&]() {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (menu != nullptr &&
            menu->objectName() ==
                QStringLiteral("viewport_operations_context_menu")) {
          ++opened_menus;
          menu->close();
        }
      });
      popup_observer.start(0);
      right_button(QEvent::MouseButtonRelease);
      process_input();
      QApplication::processEvents();
      popup_observer.stop();
      return opened_menus;
    };
    process_input();
    const auto selection_before = editor.selection().summary();
    const auto scene_selection_before = session.scene().selected_entities();
    const auto history_before = editor.history().snapshot();
    const auto pose_before = camera.pose();
    right_button(QEvent::MouseButtonPress);
    process_input();
    ASSERT_TRUE(panel.command_input_context().fly_navigation_active);
    movement_key(QEvent::KeyPress);
    process_input();
    ASSERT_GT(quader::length_squared(camera.pose().eye - pose_before.eye), 0.0f);
    movement_key(QEvent::KeyRelease);
    process_input();
    process_input();
    ASSERT_TRUE(panel.command_input_context().fly_navigation_active);
    const auto projected_target = camera.project_world_point(target);
    ASSERT_TRUE(projected_target.visible);
    EXPECT_NEAR(projected_target.position.x,
                local.x() * surface->devicePixelRatio(), 0.01);
    EXPECT_NEAR(projected_target.position.y,
                local.y() * surface->devicePixelRatio(), 0.01);

    EXPECT_EQ(release_and_count_menus(), 0);
    EXPECT_FALSE(panel.command_input_context().navigation_active);
    const auto selection_after = editor.selection().summary();
    EXPECT_EQ(selection_after.mode, selection_before.mode);
    EXPECT_EQ(selection_after.active_kind, selection_before.active_kind);
    EXPECT_EQ(selection_after.active_object_id,
              selection_before.active_object_id);
    EXPECT_EQ(selection_after.active_vertex_id,
              selection_before.active_vertex_id);
    EXPECT_EQ(selection_after.selection_revision,
              selection_before.selection_revision);
    EXPECT_EQ(session.scene().selected_entities(), scene_selection_before);
    expect_history_unchanged(editor.history().snapshot(), history_before);
    EXPECT_EQ(session.history().undo_count(), 0u);

    send_visible_viewport_event(surface, &enter);
    send_visible_viewport_event(surface, &focus);
    right_button(QEvent::MouseButtonPress);
    process_input();
    EXPECT_EQ(release_and_count_menus(), 1);
  }
}

TEST(QtViewportSurfaceInputTests,
     FlyReleaseDoesNotPublishTheCenteredPointerAsHoverOrComponentCandidate) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-viewport-fly-release-hover-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  for (const auto mode : {quader::QuaderSelectionMode::Object,
                          quader::QuaderSelectionMode::Vertex,
                          quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
    const auto box = editor.modeling().create_box(
        {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Centered navigation target");
    ASSERT_TRUE(box.ok()) << box.error_message();
    ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    ASSERT_TRUE(editor.selection().clear().ok());
    ASSERT_TRUE(editor.selection().clear_hover().ok());
    session.history().clear();

    quader::QtViewportPanel panel;
    panel.attach_workspace(session.viewport_workspace());
    auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
    ASSERT_NE(surface, nullptr);
    resize_viewport_surface(panel, *surface, {800, 600});
    auto &camera = session.viewport_workspace().camera(
        quader::ViewportPaneId::Perspective);
    const quader::QVec3 target =
        mode == quader::QuaderSelectionMode::Vertex ? quader::QVec3{1, 1, 1}
        : mode == quader::QuaderSelectionMode::Edge ? quader::QVec3{1, 0, 1}
                                                  : quader::QVec3{0, 0, 1};
    ASSERT_TRUE(camera.restore_perspective_pose(
        {.eye = target + quader::normalize(target) * 8.0f,
         .target = target,
         .up = {0, 1, 0}}));
    const QPointF original_local{40, 40};
    const QPointF center_local{400, 300};
    const auto mouse = [&](QEvent::Type type, QPointF local,
                           Qt::MouseButton button, Qt::MouseButtons held) {
      QMouseEvent event(type, local, local + QPointF{100, 100}, button, held,
                       Qt::NoModifier);
      send_visible_viewport_event(surface, &event);
    };
    const auto movement_key = [&](QEvent::Type type) {
      QKeyEvent event(type, Qt::Key_W, Qt::NoModifier);
      send_visible_viewport_event(surface, &event);
    };
    const auto process_input = [&]() {
      panel.process_input(editor, session.scene(), session.assets(),
                          session.modeling(), {}, session.native_tools(),
                          &session.point_placement(), 1.0 / 600.0);
    };
    const auto expect_empty_hover = [&]() {
      EXPECT_EQ(editor.selection().hover_summary().target.kind,
                quader::QuaderActiveSelectionKind::None);
      EXPECT_FALSE(session.selection().resolved_hover_target().has_value());
      // Component candidates draw persistent cyan source wire even when no
      // individual face, edge, or vertex remains under the restored cursor.
      EXPECT_LE(session.modeling().selection_hover_summary().candidate_object_id,
                0);
    };
    QEnterEvent enter(original_local, original_local,
                      original_local + QPointF{100, 100});
    send_visible_viewport_event(surface, &enter);
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &focus);
    mouse(QEvent::MouseMove, original_local, Qt::NoButton, Qt::NoButton);
    process_input();
    expect_empty_hover();
    const auto selection_before = editor.selection().summary();
    const auto scene_selection_before = session.scene().selected_entities();
    const auto history_before = editor.history().snapshot();
    const auto pose_before = camera.pose();

    mouse(QEvent::MouseButtonPress, original_local, Qt::RightButton,
          Qt::RightButton);
    process_input();
    ASSERT_TRUE(panel.command_input_context().fly_navigation_active);
    movement_key(QEvent::KeyPress);
    process_input();
    ASSERT_GT(quader::length_squared(camera.pose().eye - pose_before.eye), 0.0f);
    movement_key(QEvent::KeyRelease);
    process_input();
    process_input();
    ASSERT_TRUE(panel.command_input_context().fly_navigation_active);
    expect_empty_hover();
    const auto projected_target = camera.project_world_point(target);
    ASSERT_TRUE(projected_target.visible);
    EXPECT_NEAR(projected_target.position.x,
                center_local.x() * surface->devicePixelRatio(), 0.01);
    EXPECT_NEAR(projected_target.position.y,
                center_local.y() * surface->devicePixelRatio(), 0.01);

    // Captured RMB releases arrive at the center before the platform restores
    // the real cursor. Deliver that packet explicitly for headless hosts too.
    mouse(QEvent::MouseButtonRelease, center_local, Qt::RightButton,
          Qt::NoButton);
    process_input();
    EXPECT_FALSE(panel.command_input_context().navigation_active);
    expect_empty_hover();

    // Simulate the restored pointer without requiring a native grab or moving
    // the user's desktop cursor. The erroneous component candidate must never
    // survive into ordinary idle hover work.
    mouse(QEvent::MouseMove, original_local, Qt::NoButton, Qt::NoButton);
    process_input();
    process_input();
    expect_empty_hover();
    EXPECT_EQ(editor.selection().summary().selection_revision,
              selection_before.selection_revision);
    EXPECT_FALSE(editor.selection().summary().has_selection);
    EXPECT_EQ(session.scene().selected_entities(), scene_selection_before);
    expect_history_unchanged(editor.history().snapshot(), history_before);
    EXPECT_EQ(session.history().undo_count(), 0u);

    // Hover becomes eligible again when the real pointer reaches the mesh.
    mouse(QEvent::MouseMove, center_local, Qt::NoButton, Qt::NoButton);
    process_input();
    if (mode == quader::QuaderSelectionMode::Object) {
      EXPECT_EQ(editor.selection().hover_summary().target.object_id,
                box.value().modeling_object_id);
    } else {
      EXPECT_EQ(session.modeling().selection_hover_summary().candidate_object_id,
                box.value().modeling_object_id);
    }
    EXPECT_FALSE(editor.selection().summary().has_selection);
    EXPECT_EQ(editor.selection().summary().selection_revision,
              selection_before.selection_revision);
  }
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
  resize_viewport_surface(panel, *surface, {800, 600});
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);

  const auto process_input = [&]() {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  const auto send_mouse = [&](QEvent::Type type, Qt::MouseButtons buttons) {
    QMouseEvent event(type, local, global, Qt::LeftButton, buttons,
                       Qt::NoModifier);
    send_visible_viewport_event(surface, &event);
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
    const auto undo_count_before = session.history().undo_count();
    const auto first_click_changes = active_before != objects[0] ? 1u : 0u;
    const auto depth_click_changes = expected != objects[0] ? 1u : 0u;
    send_mouse(QEvent::MouseButtonPress, Qt::LeftButton);
    send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
    EXPECT_EQ(session.scene().selected_entities(),
              std::vector<quader::EntityId>{objects[0]});
    EXPECT_EQ(session.scene().primary_selection(), objects[0]);
    EXPECT_EQ(session.scene().selection_revision(),
              selection_revision_before + first_click_changes);
    EXPECT_EQ(session.history().undo_count(),
              undo_count_before + first_click_changes);
    send_mouse(QEvent::MouseButtonDblClick, Qt::LeftButton);
    send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
    EXPECT_EQ(session.scene().primary_selection(), expected);
    EXPECT_EQ(session.scene().selection_revision(),
              selection_revision_before + first_click_changes + depth_click_changes);
    EXPECT_EQ(session.history().undo_count(), undo_count_before + 1u);
    process_input();
    EXPECT_EQ(session.scene().primary_selection(), expected);
    EXPECT_EQ(session.scene().selection_revision(),
              selection_revision_before + first_click_changes + depth_click_changes);
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
      resize_viewport_surface(panel, *surface, {800, 600});
      const QPointF local{400.0, 300.0};
      const QPointF global{500.0, 400.0};
      QEnterEvent enter(local, local, global);
      send_visible_viewport_event(surface, &enter);
      const auto process_input = [&]() {
        panel.process_input(
            editor, session.scene(), session.assets(), session.modeling(), {},
            session.native_tools(), &session.point_placement(), 1.0 / 60.0);
      };
      const auto send_mouse = [&](QEvent::Type type, Qt::MouseButtons buttons) {
        QMouseEvent event(type, local, global, Qt::LeftButton, buttons,
                           Qt::NoModifier);
        send_visible_viewport_event(surface, &event);
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
        const auto undo_count_before = session.history().undo_count();
        const auto first_click_changes =
            !coalesced && active_before != room.value().entity ? 1u : 0u;
        const auto depth_click_changes =
            coalesced || expected != room.value().entity ? 1u : 0u;
        send_mouse(QEvent::MouseButtonPress, Qt::LeftButton);
        send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
        if (coalesced) {
          EXPECT_EQ(session.scene().selected_entities(), selected_before);
          EXPECT_EQ(session.scene().primary_selection(), active_before);
          expect_history_unchanged(editor.history().snapshot(), history_before);
        } else {
          EXPECT_EQ(session.scene().selected_entities(),
                    std::vector<quader::EntityId>{room.value().entity});
          EXPECT_EQ(session.scene().primary_selection(), room.value().entity);
        }
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + first_click_changes);
        EXPECT_EQ(session.history().undo_count(),
                  undo_count_before + first_click_changes);
        send_mouse(QEvent::MouseButtonDblClick, Qt::LeftButton);
        send_mouse(QEvent::MouseButtonRelease, Qt::NoButton);
        process_input();
        EXPECT_EQ(session.scene().primary_selection(), expected);
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + first_click_changes + depth_click_changes);
        EXPECT_EQ(session.history().undo_count(), undo_count_before + 1u);
        process_input();
        EXPECT_EQ(session.scene().primary_selection(), expected);
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + first_click_changes + depth_click_changes);
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
      resize_viewport_surface(panel, *surface, {800, 600});
      const QPointF local{400.0, 300.0};
      const QPointF global{500.0, 400.0};
      QEnterEvent enter(local, local, global);
      send_visible_viewport_event(surface, &enter);
      QFocusEvent focus(QEvent::FocusIn);
      send_visible_viewport_event(surface, &focus);
      const auto process_input = [&]() {
        panel.process_input(
            editor, session.scene(), session.assets(), session.modeling(), {},
            session.native_tools(), &session.point_placement(), 1.0 / 60.0);
      };
      const auto double_click = [&](
          Qt::KeyboardModifiers modifiers,
          std::initializer_list<quader::EntityId> first_click_entities,
          quader::EntityId first_click_active,
          bool depth_changes_after_first = true,
          bool selection_changes = true) {
        const auto selected_before = session.scene().selected_entities();
        const auto active_before = session.scene().primary_selection();
        const auto selection_revision_before = session.scene().selection_revision();
        const auto history_before = editor.history().snapshot();
        const auto undo_count_before = session.history().undo_count();
        std::vector<quader::EntityId> selected_after_first(first_click_entities);
        std::sort(selected_after_first.begin(), selected_after_first.end());
        const auto first_click_changes =
            selected_after_first != selected_before || first_click_active != active_before
                ? 1u
                : 0u;
        const bool separate_first_release = frame_schedules[schedule][1];
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
          send_visible_viewport_event(surface, &event);
          if (frame_schedules[schedule][index]) {
            process_input();
          }
          if (index < 2u) {
            const bool first_click_published = index == 1u && separate_first_release;
            EXPECT_EQ(session.scene().selected_entities(),
                      first_click_published ? selected_after_first : selected_before);
            EXPECT_EQ(session.scene().primary_selection(),
                      first_click_published ? first_click_active : active_before);
            EXPECT_EQ(session.scene().selection_revision(),
                      selection_revision_before +
                          (first_click_published ? first_click_changes : 0u));
            EXPECT_EQ(session.history().undo_count(),
                      undo_count_before +
                          (first_click_published ? first_click_changes : 0u));
            if (!first_click_published || first_click_changes == 0u) {
              expect_history_unchanged(editor.history().snapshot(), history_before);
            }
          }
        }
        const auto publication_count =
            separate_first_release
                ? first_click_changes + (depth_changes_after_first ? 1u : 0u)
                : (selection_changes ? 1u : 0u);
        EXPECT_EQ(session.scene().selection_revision(),
                  selection_revision_before + publication_count);
        EXPECT_EQ(session.history().undo_count(),
                  undo_count_before + (selection_changes ? 1u : 0u));
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

      double_click(Qt::ShiftModifier,
                   {front.value().entity, unrelated.value().entity},
                   front.value().entity);
      expect_selection({unrelated.value().entity, behind.value().entity});

      ASSERT_TRUE(editor.selection()
                      .select_object(front.value(), quader::QuaderSelectionEdit::Add)
                      .ok());
      double_click(Qt::ControlModifier,
                   {behind.value().entity, unrelated.value().entity},
                   unrelated.value().entity);
      expect_selection({unrelated.value().entity, front.value().entity});

      // Further Remove gestures advance through the stack. An already removed
      // mesh stays unselected when the traversal wraps back to it.
      double_click(Qt::ControlModifier, {unrelated.value().entity},
                   unrelated.value().entity, false);
      expect_selection({unrelated.value().entity});
      double_click(Qt::ControlModifier, {unrelated.value().entity},
                   unrelated.value().entity, false, false);
      expect_selection({unrelated.value().entity});

      // Changing the operation restarts at the first mesh behind the front hit.
      double_click(Qt::ShiftModifier,
                   {front.value().entity, unrelated.value().entity},
                   front.value().entity);
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
  send_visible_viewport_event(surface, &wheel_up);
  process_input();
  EXPECT_EQ(session.viewport_workspace().grid_preset(), default_preset + 1);

  QWheelEvent wheel_down(local, global, {}, QPoint{-120, 0}, Qt::NoButton,
                         Qt::AltModifier, Qt::NoScrollPhase, false);
  send_visible_viewport_event(surface, &wheel_down);
  process_input();
  EXPECT_EQ(session.viewport_workspace().grid_preset(), default_preset);

  ASSERT_TRUE(session.viewport_workspace().set_grid_preset(2));
  // The retired Alt+MMB reset gesture must leave the shared grid size unchanged.
  const quader::ViewportCameraPose camera_before_click =
      session.viewport_workspace()
          .camera(quader::ViewportPaneId::Perspective)
          .pose();
  QMouseEvent middle_press(QEvent::MouseButtonPress, local, global,
                           Qt::MiddleButton, Qt::MiddleButton,
                           Qt::AltModifier);
  send_visible_viewport_event(surface, &middle_press);
  process_input();

  QMouseEvent middle_release(QEvent::MouseButtonRelease, local, global,
                             Qt::MiddleButton, Qt::NoButton,
                             Qt::AltModifier);
  send_visible_viewport_event(surface, &middle_release);
  process_input();
  EXPECT_EQ(session.viewport_workspace().grid_preset(), 2);
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
  resize_viewport_surface(panel, *surface, {800, 600});
  const QPointF center{400.0, 300.0};
  const QPointF center_global{500.0, 400.0};
  QEnterEvent enter(center, center, center_global);
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);

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

  // Plain wheel uses the actual center-view surface, independent of the
  // scene-root origin and the stale target 158 units away.
  QWheelEvent wheel_in(center, center_global, {}, QPoint{0, 120}, Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
  send_visible_viewport_event(surface, &wheel_in);
  process_input();
  EXPECT_NEAR(camera.state().distance, 4.0f / 1.18f, 0.01f);

  restore_stale_pose();
  const quader::ViewportScreenPoint anchor_before =
      camera.project_world_point({0.0f, 0.0f, -4.0f});
  ASSERT_TRUE(anchor_before.visible);

  QMouseEvent middle_press(QEvent::MouseButtonPress, center, center_global,
                           Qt::MiddleButton, Qt::MiddleButton,
                           Qt::NoModifier);
  send_visible_viewport_event(surface, &middle_press);
  process_input();
  const quader::ViewportCameraPose before_orbit = camera.pose();
  EXPECT_NEAR(camera.state().distance, 158.0f, 0.01f);

  const QPointF moved{405.0, 300.0};
  const QPointF moved_global{505.0, 400.0};
  QMouseEvent middle_move(QEvent::MouseMove, moved, moved_global, Qt::NoButton,
                          Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &middle_move);
  process_input();

  EXPECT_TRUE(panel.command_input_context().navigation_active);
  EXPECT_TRUE(panel.command_input_context().orbit_navigation_active);
  EXPECT_FALSE(panel.command_input_context().pan_navigation_active);

  const QPointF moved_again{410.0, 300.0};
  const QPointF moved_again_global{510.0, 400.0};
  QMouseEvent middle_move_again(QEvent::MouseMove, moved_again,
                                moved_again_global, Qt::NoButton,
                                Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &middle_move_again);
  QMouseEvent middle_release(QEvent::MouseButtonRelease, moved_again,
                             moved_again_global, Qt::MiddleButton,
                             Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &middle_release);
  const QPointF post_release{700.0, 300.0};
  const QPointF post_release_global{800.0, 400.0};
  QMouseEvent post_release_move(QEvent::MouseMove, post_release,
                                post_release_global, Qt::NoButton,
                                Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &post_release_move);
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
  send_visible_viewport_event(surface, &pan_press);
  process_input();
  const QPointF panned{430.0, 310.0};
  const QPointF panned_global{530.0, 410.0};
  QMouseEvent pan_move(QEvent::MouseMove, panned, panned_global, Qt::NoButton,
                       Qt::MiddleButton, Qt::ShiftModifier);
  send_visible_viewport_event(surface, &pan_move);
  process_input();
  EXPECT_TRUE(panel.command_input_context().navigation_active);
  EXPECT_TRUE(panel.command_input_context().pan_navigation_active);
  EXPECT_FALSE(panel.command_input_context().orbit_navigation_active);
  QMouseEvent pan_release(QEvent::MouseButtonRelease, panned, panned_global,
                          Qt::MiddleButton, Qt::NoButton,
                          Qt::ShiftModifier);
  send_visible_viewport_event(surface, &pan_release);
  process_input();
  const quader::ViewportCameraPose after_pan = camera.pose();
  EXPECT_FALSE(panel.command_input_context().navigation_active);
  EXPECT_GT(quader::length(after_pan.target - before_pan.target), 0.01f);

  const float distance_before_wheel = camera.state().distance;
  const quader::QVec3 target_before_wheel = camera.state().target;
  QWheelEvent wheel_out(panned, panned_global, {}, QPoint{0, -120},
                        Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                        false);
  send_visible_viewport_event(surface, &wheel_out);
  process_input();
  EXPECT_NEAR(camera.state().distance, distance_before_wheel * 1.18f,
              0.001f);
  QWheelEvent wheel_back_in(panned, panned_global, {}, QPoint{0, 120},
                            Qt::NoButton, Qt::NoModifier,
                            Qt::NoScrollPhase, false);
  send_visible_viewport_event(surface, &wheel_back_in);
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
  send_visible_viewport_event(surface, &fresh_orbit_press);
  process_input();
  const quader::ViewportCameraPose pan_zoom_pose = camera.pose();
  const QPointF fresh_orbit_moved{orbit_press_local.x() + 5.0,
                                  orbit_press_local.y()};
  const QPointF fresh_orbit_moved_global{orbit_press_global.x() + 5.0,
                                         orbit_press_global.y()};
  QMouseEvent fresh_orbit_move(
      QEvent::MouseMove, fresh_orbit_moved, fresh_orbit_moved_global,
      Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &fresh_orbit_move);
  process_input();
  QMouseEvent fresh_orbit_release(
      QEvent::MouseButtonRelease, fresh_orbit_moved,
      fresh_orbit_moved_global, Qt::MiddleButton, Qt::NoButton,
      Qt::NoModifier);
  send_visible_viewport_event(surface, &fresh_orbit_release);
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
  send_visible_viewport_event(surface, &fallback_press);
  process_input();
  QMouseEvent fallback_move(QEvent::MouseMove, moved, moved_global,
                            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &fallback_move);
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
  send_visible_viewport_event(surface, &fallback_release);
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
  send_visible_viewport_event(surface, &isolated_press);
  process_input();
  QMouseEvent isolated_move(QEvent::MouseMove, moved, moved_global,
                            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &isolated_move);
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
  send_visible_viewport_event(surface, &isolated_release);
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
  send_visible_viewport_event(surface, &occluded_press);
  process_input();
  QMouseEvent occluded_move(QEvent::MouseMove, moved, moved_global,
                            Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &occluded_move);
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
  send_visible_viewport_event(surface, &occluded_release);
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
  send_visible_viewport_event(surface, &far_selection_press);
  process_input();
  const QPointF far_selection_moved{25.0, 20.0};
  const QPointF far_selection_moved_global{125.0, 120.0};
  QMouseEvent far_selection_move(
      QEvent::MouseMove, far_selection_moved,
      far_selection_moved_global, Qt::NoButton, Qt::MiddleButton,
      Qt::NoModifier);
  send_visible_viewport_event(surface, &far_selection_move);
  process_input();
  EXPECT_NEAR(camera.state().distance, 4.0f, 0.0001f);
  QMouseEvent far_selection_release(
      QEvent::MouseButtonRelease, far_selection_moved,
      far_selection_moved_global, Qt::MiddleButton, Qt::NoButton,
      Qt::NoModifier);
  send_visible_viewport_event(surface, &far_selection_release);
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
  send_visible_viewport_event(surface, &hierarchy_press);
  process_input();
  QMouseEvent hierarchy_move(QEvent::MouseMove, moved, moved_global,
                             Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &hierarchy_move);
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
  send_visible_viewport_event(surface, &hierarchy_release);
  process_input();

  // Empty-background orbit falls back to the visible center surface. The
  // nearby box remains authoritative despite distant scene bounds.
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
  send_visible_viewport_event(surface, &blank_press);
  process_input();
  EXPECT_EQ(quader::viewport_camera_pose_persistence_signature(camera.pose()),
            quader::viewport_camera_pose_persistence_signature(blank_before));

  const QPointF blank_moved{25.0, 20.0};
  const QPointF blank_moved_global{125.0, 120.0};
  QMouseEvent blank_move(QEvent::MouseMove, blank_moved, blank_moved_global,
                         Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &blank_move);
  process_input();

  EXPECT_NEAR(camera.state().distance, 4.0f, 0.0001f);
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
  send_visible_viewport_event(surface, &blank_release);
  process_input();
}

TEST(QtViewportSurfaceInputTests,
     WheelTicksKeepIdleAndToolHintsStableAcrossOwningAndPassivePanes) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-wheel-hint-stability-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::LocalizationService localization;

  for (const bool active_box : {false, true}) {
    SCOPED_TRACE(active_box);
    NavigationPanelTestContext context;
    ASSERT_NE(context.surface, nullptr);
    auto &workspace = context.session.viewport_workspace();
    ASSERT_TRUE(workspace.set_layout(quader::ViewportLayoutId::FourQuad));
    context.panel.refresh_workspace_layout();
    if (active_box) {
      ASSERT_TRUE(context.editor.tools()
                      .activate(quader::ViewportToolId::CreatePrimitive).ok());
      ASSERT_TRUE(context.editor.tools().pane_owns_executing_tool(
          quader::ViewportPaneId::Perspective));
    }
    const auto origin = context.editor.tools().active_settings_presentation_origin();
    const auto tool_revision = context.editor.tools().active_tool_revision();
    const auto history = context.editor.history().snapshot();
    quader::QtOperationInputHints row;
    row.setAttribute(Qt::WA_DontShowOnScreen);
    row.resize(1600, 40);
    InputHintVisibilityProbe visibility;
    row.installEventFilter(&visibility);
    const auto publish = [&]() {
      context.process();
      row.refresh(context.editor.tools(), context.editor.commands(),
                  localization, context.panel.operation_input_context());
    };

    for (const auto pane : {quader::ViewportPaneId::Perspective,
                            quader::ViewportPaneId::Top}) {
      SCOPED_TRACE(static_cast<int>(pane));
      QEvent leave(QEvent::Leave);
      QFocusEvent blur(QEvent::FocusOut);
      send_visible_viewport_event(context.surface, &leave);
      send_visible_viewport_event(context.surface, &blur);
      context.surface = context.panel.surface(pane);
      ASSERT_NE(context.surface, nullptr);
      resize_viewport_surface(context.panel, *context.surface, {800, 600});
      ASSERT_TRUE(context.editor.viewports().focus_pane(pane).ok());
      QEnterEvent enter({20, 20}, {20, 20}, {120, 120});
      QFocusEvent focus(QEvent::FocusIn);
      send_visible_viewport_event(context.surface, &enter);
      send_visible_viewport_event(context.surface, &focus);
      auto &camera = workspace.camera(pane);
      camera.focus_origin(64.0f);
      if (active_box && pane == quader::ViewportPaneId::Top) {
        ASSERT_TRUE(context.editor.tools().pane_owns_executing_tool(pane));
      }

      for (const auto modifiers : {Qt::NoModifier, Qt::ControlModifier}) {
        SCOPED_TRACE(static_cast<int>(modifiers));
        // Prime the same held modifier state before comparing wheel/idle frames.
        context.mouse(QEvent::MouseMove, {20, 20}, Qt::NoButton,
                      Qt::NoButton, modifiers);
        publish();
        publish();
        ASSERT_TRUE(context.panel.operation_input_context().viewport_available);
        ASSERT_EQ(context.panel.operation_input_context().control,
                  modifiers == Qt::ControlModifier);
        ASSERT_FALSE(row.hints().empty());
        ASSERT_FALSE(row.isHidden());
        const auto expected_hints = row.hints();
        const auto projection = row.projection_revision();
        visibility.hides = 0;
        const auto native_hints = context.editor.tools().active_input_hints(
            context.panel.operation_input_context());
        if (active_box) {
          EXPECT_EQ(std::ranges::any_of(native_hints, [](const auto &hint) {
                      return hint.label_key == "operation.hints.cancel_creation";
                    }),
                    true);
        }
        const auto expect_stable = [&]() {
          EXPECT_EQ(row.hints(), expected_hints);
          EXPECT_EQ(row.projection_revision(), projection);
          EXPECT_FALSE(row.isHidden());
          EXPECT_EQ(visibility.hides, 0);
          EXPECT_EQ(context.editor.tools().active_tool_revision(), tool_revision);
          EXPECT_EQ(context.editor.tools().active_settings_presentation_origin(),
                    origin);
          expect_history_unchanged(context.editor.history().snapshot(), history);
        };
        for (const int delta : {120, -120, 120, -120}) {
          SCOPED_TRACE(delta);
          const auto zoom_scale = [&]() {
            return camera.state().projection == quader::ViewportProjection::Orthographic
                ? camera.state().orthographic_size : camera.state().distance;
          };
          const float before_zoom = zoom_scale();
          context.wheel({20, 20}, delta, modifiers);
          publish();
          if (delta > 0) {
            EXPECT_LT(zoom_scale(), before_zoom);
          } else {
            EXPECT_GT(zoom_scale(), before_zoom);
          }
          expect_stable();
          for (int idle_frame = 0; idle_frame < 3; ++idle_frame) {
            publish();
            expect_stable();
          }
        }
      }
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     FlyWheelSpeedChangesKeepTheFlyHintRowStable) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-fly-wheel-hint-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  (void)context.camera.set_default_fly_speed(32.0f);
  context.camera.reset_fly_speed();
  context.process();
  context.mouse(QEvent::MouseButtonPress, {400, 300}, Qt::RightButton,
                Qt::RightButton);
  context.process();
  context.mouse(QEvent::MouseMove, {420, 300}, Qt::NoButton, Qt::RightButton);
  context.process();
  ASSERT_EQ(context.panel.operation_input_context().navigation,
            quader::QuaderInputNavigation::Fly);
  quader::LocalizationService localization;
  quader::QtOperationInputHints row;
  row.setAttribute(Qt::WA_DontShowOnScreen);
  row.resize(1600, 40);
  InputHintVisibilityProbe visibility;
  row.installEventFilter(&visibility);
  const auto publish = [&]() {
    context.process();
    row.refresh(context.editor.tools(), context.editor.commands(), localization,
                context.panel.operation_input_context());
  };
  publish();
  ASSERT_FALSE(row.hints().empty());
  const auto expected_hints = row.hints();
  const auto projection = row.projection_revision();
  const auto native_hints = context.editor.tools().active_input_hints(
      context.panel.operation_input_context());
  ASSERT_TRUE(std::ranges::any_of(native_hints, [](const auto &hint) {
    return hint.label_key == "operation.hints.navigation.fly_speed";
  }));
  for (const int delta : {120, -120, 120, -120}) {
    const float before_speed = context.camera.fly_speed();
    context.wheel({420, 300}, delta);
    publish();
    if (delta > 0) {
      EXPECT_GT(context.camera.fly_speed(), before_speed);
    } else {
      EXPECT_LT(context.camera.fly_speed(), before_speed);
    }
    for (int frame = 0; frame < 4; ++frame) {
      EXPECT_EQ(context.panel.operation_input_context().navigation,
                quader::QuaderInputNavigation::Fly);
      EXPECT_EQ(row.hints(), expected_hints);
      EXPECT_EQ(row.projection_revision(), projection);
      EXPECT_FALSE(row.isHidden());
      EXPECT_EQ(visibility.hides, 0);
      publish();
    }
  }
  context.mouse(QEvent::MouseButtonRelease, {420, 300}, Qt::RightButton,
                Qt::NoButton);
  context.process();
}

TEST(QtViewportSurfaceInputTests,
     WheelUsesVisibleSurfaceAndRecoversTinyNavigationScale) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-visible-navigation-scale-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  const auto subject = context.editor.modeling().create_box(
      {.min = {-1, -1, -6}, .max = {1, 1, -4}});
  ASSERT_TRUE(subject.ok());
  // This object is behind the eye and far outside the view. Together with the
  // subject it puts the whole-scene bounds center only 0.1 units ahead.
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {999, -1, 3.8f}, .max = {1001, 1, 5.8f}}).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {}, .target = {0, 0, -100}, .up = {0, 1, 0}}));
  context.process();

  const quader::ViewportCameraPose before = context.camera.pose();
  context.wheel({20, 20}, 120);
  context.process();
  EXPECT_NEAR(context.camera.state().distance, 4.0f / 1.18f, 0.001f);
  EXPECT_GT(quader::length(context.camera.pose().eye - before.eye), 0.5f);

  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {}, .target = {0, 0, -0.05f}, .up = {0, 1, 0}}));
  context.wheel({20, 20}, -120);
  context.process();
  EXPECT_NEAR(context.camera.state().distance, 4.0f * 1.18f, 0.001f);
  EXPECT_GT(context.camera.pose().eye.z, 0.5f);

  // Moving to an off-center subject also repairs tiny depth when the center
  // is empty; plain wheel still moves straight along the view direction.
  ASSERT_TRUE(context.session.scene().set_visible(subject.value().entity, false));
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {3.8f, -0.2f, -10.2f}, .max = {4.2f, 0.2f, -10}}).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {}, .target = {0, 0, -0.05f}, .up = {0, 1, 0}}));
  const auto off_center = context.camera.project_world_point({4, 0, -10});
  ASSERT_TRUE(off_center.visible);
  const qreal pixel_ratio = context.surface->devicePixelRatio();
  context.wheel({off_center.position.x / pixel_ratio,
                  off_center.position.y / pixel_ratio}, -120);
  context.process();
  EXPECT_NEAR(context.camera.state().distance, 10.0f * 1.18f, 0.001f);
  EXPECT_NEAR(context.camera.pose().eye.x, 0.0f, 0.0001f);
  ASSERT_TRUE(context.session.scene().set_visible(subject.value().entity, true));

  // A clipped foreground surface must not hide the usable surface behind it.
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {-1, -1, -0.04f}, .max = {1, 1, -0.01f}}).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {}, .target = {0, 0, -100}, .up = {0, 1, 0}}));
  context.wheel({20, 20}, 120);
  context.process();
  EXPECT_NEAR(context.camera.state().distance, 4.0f / 1.18f, 0.001f);

  // Backfaces of a one-sided enclosing mesh are not rendered and must not
  // replace the visible subject's navigation depth.
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {}, .target = {0, 0, -100}, .up = {0, 1, 0}}));
  context.wheel({20, 20}, 120);
  context.process();
  EXPECT_NEAR(context.camera.state().distance, 4.0f / 1.18f, 0.001f);
}

TEST(QtViewportSurfaceInputTests,
     PanFlyThenBackgroundOrbitUsesTheNearbySubjectWithoutReset) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-navigation-transition-depth-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {-3, -3, -6}, .max = {3, 3, -4}}).ok());
  // After the fly move, this offscreen object leaves the global bounds center
  // behind the eye. The old fallback then kept the entire 100-unit orbit.
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {999, -1, 98}, .max = {1001, 1, 100}}).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {0, 0, 96}, .target = {0, 0, -4}, .up = {0, 1, 0}}));
  (void)context.camera.set_default_fly_speed(96.0f);
  context.camera.reset_fly_speed();
  context.process();

  context.mouse(QEvent::MouseButtonPress, {400, 300}, Qt::MiddleButton,
                Qt::MiddleButton, Qt::ShiftModifier);
  context.process();
  context.mouse(QEvent::MouseMove, {401, 300}, Qt::NoButton,
                Qt::MiddleButton, Qt::ShiftModifier);
  context.process();
  context.mouse(QEvent::MouseButtonRelease, {401, 300}, Qt::MiddleButton,
                Qt::NoButton, Qt::ShiftModifier);
  context.process();
  ASSERT_NEAR(context.camera.state().distance, 100.0f, 0.001f);

  context.mouse(QEvent::MouseButtonPress, {400, 300}, Qt::RightButton,
                Qt::RightButton);
  context.process();
  QKeyEvent forward_press(QEvent::KeyPress, Qt::Key_W, Qt::NoModifier);
  send_visible_viewport_event(context.surface, &forward_press);
  for (int frame = 0; frame < 10; ++frame) {
    context.process(0.1);
  }
  QKeyEvent forward_release(QEvent::KeyRelease, Qt::Key_W, Qt::NoModifier);
  send_visible_viewport_event(context.surface, &forward_release);
  context.mouse(QEvent::MouseButtonRelease, {400, 300}, Qt::RightButton,
                Qt::NoButton);
  context.process();
  ASSERT_NEAR(context.camera.pose().eye.z, 0.0f, 0.001f);
  ASSERT_NEAR(context.camera.state().distance, 100.0f, 0.001f);

  // The next press misses the subject. Its center-view surface must repair
  // the stale fly depth before even a small orbit moves the eye.
  const quader::ViewportCameraPose before_orbit = context.camera.pose();
  context.mouse(QEvent::MouseButtonPress, {20, 20}, Qt::MiddleButton,
                Qt::MiddleButton);
  context.process();
  context.mouse(QEvent::MouseMove, {25, 20}, Qt::NoButton, Qt::MiddleButton);
  context.process();
  context.mouse(QEvent::MouseButtonRelease, {25, 20}, Qt::MiddleButton,
                Qt::NoButton);
  context.process();
  EXPECT_NEAR(context.camera.state().distance, 4.0f, 0.001f);
  const float angle = 5.0f * static_cast<float>(
      quader::viewport_host_defaults().orbit_radians_per_pixel);
  EXPECT_LE(quader::length(context.camera.pose().eye - before_orbit.eye),
            8.0f * std::sin(angle * 0.5f) + 0.001f);
  EXPECT_FALSE(context.panel.command_input_context().navigation_active);
}

TEST(QtViewportSurfaceInputTests,
     QueuedCursorWheelEventsRetainTheirOwnSurfaceAnchors) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-navigation-wheel-anchor-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  NavigationPanelTestContext context;
  ASSERT_NE(context.surface, nullptr);
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {-1.2f, -0.2f, -2.2f}, .max = {-0.8f, 0.2f, -2.0f}}).ok());
  ASSERT_TRUE(context.editor.modeling().create_box(
      {.min = {49, -1, -101}, .max = {51, 1, -100}}).ok());
  ASSERT_TRUE(context.editor.selection().clear().ok());
  ASSERT_TRUE(context.camera.restore_perspective_pose(
      {.eye = {}, .target = {0, 0, -100}, .up = {0, 1, 0}}));
  context.process();

  constexpr quader::QVec3 kNearAnchor{-1, 0, -2};
  constexpr quader::QVec3 kFarAnchor{50, 0, -100};
  const auto near_screen = context.camera.project_world_point(kNearAnchor);
  const auto far_screen = context.camera.project_world_point(kFarAnchor);
  ASSERT_TRUE(near_screen.visible);
  ASSERT_TRUE(far_screen.visible);
  quader::ViewportCameraController expected;
  expected.set_viewport_size(800, 600);
  ASSERT_TRUE(expected.restore_perspective_pose(context.camera.pose()));
  expected.scroll_to_cursor(1.0f, near_screen.position, kNearAnchor);
  expected.scroll_to_cursor(1.0f, far_screen.position, kFarAnchor);

  const qreal pixel_ratio = context.surface->devicePixelRatio();
  context.wheel({near_screen.position.x / pixel_ratio,
                  near_screen.position.y / pixel_ratio},
                120, Qt::ControlModifier);
  context.wheel({far_screen.position.x / pixel_ratio,
                  far_screen.position.y / pixel_ratio},
                120, Qt::ControlModifier);
  // Later pointer motion must not select one shared anchor for both events.
  context.mouse(QEvent::MouseMove, {400, 300}, Qt::NoButton, Qt::NoButton);
  context.process();

  EXPECT_NEAR(context.camera.state().distance, expected.state().distance, 0.01f);
  EXPECT_NEAR(quader::length(context.camera.pose().eye - expected.pose().eye),
              0.0f, 0.01f);
  EXPECT_NEAR(quader::length(context.camera.pose().target - expected.pose().target),
              0.0f, 0.01f);
}

TEST(QtViewportSurfaceInputTests,
     HeldKeyboardOriginRequiresOneNativePhysicalKey) {
#ifndef Q_OS_WIN
  GTEST_SKIP() << "Windows native key metadata supplies physical key identity.";
#else
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-held-key-origin-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QKeyEvent first_press(QEvent::KeyPress, Qt::Key_Q, Qt::ShiftModifier,
                        0x10U, 0x51U, 0U, QStringLiteral("Q"));
  QApplication::sendEvent(&surface, &first_press);
  const auto frame = surface.take_input_frame(0.0F, true);
  ASSERT_EQ(frame.input.pressed_input_chords.size(), 1U);
  const auto chord = frame.input.pressed_input_chords.front();
  EXPECT_EQ(surface.held_keyboard_key_identity(chord),
            quader::quader_keyboard_event_key_identity(first_press));
  QKeyEvent second_press(QEvent::KeyPress, Qt::Key_Q, Qt::ShiftModifier,
                         0x20U, 0x51U, 0U, QStringLiteral("Q"));
  QApplication::sendEvent(&surface, &second_press);
  EXPECT_FALSE(surface.held_keyboard_key_identity(chord).has_value());
  surface.abandon_input();
  QKeyEvent synthetic_press(QEvent::KeyPress, Qt::Key_Q, Qt::ShiftModifier,
                            QStringLiteral("Q"));
  QApplication::sendEvent(&surface, &synthetic_press);
  EXPECT_FALSE(surface.held_keyboard_key_identity(chord).has_value());
#endif
}

TEST(QtViewportSurfaceInputTests,
     NativeShiftedKeysRetainTheirAcceptedChordUntilPhysicalRelease) {
#ifndef Q_OS_WIN
  GTEST_SKIP() << "Windows native key metadata supplies layout-independent keys.";
#else
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-native-key-lifecycle-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QKeyEvent override_event(QEvent::ShortcutOverride, Qt::Key_sterling,
                           Qt::ShiftModifier, 0x04U, 0x33U, 0U,
                           QString(QChar(0x00a3)));
  override_event.ignore();
  QApplication::sendEvent(&surface, &override_event);
  EXPECT_TRUE(override_event.isAccepted());
  QKeyEvent press(QEvent::KeyPress, Qt::Key_sterling, Qt::ShiftModifier,
                  0x04U, 0x33U, 0U, QString(QChar(0x00a3)));
  QApplication::sendEvent(&surface, &press);
  auto frame = surface.take_input_frame(0.0F, true);
  ASSERT_EQ(frame.input.pressed_input_chords.size(), 1U);
  const auto accepted_chord = frame.input.pressed_input_chords.front();
  EXPECT_EQ(accepted_chord.key, QuaderKeyCode::Digit3);
  EXPECT_TRUE(accepted_chord.shift);
  ASSERT_EQ(frame.input.active_input_chords.size(), 1U);
  EXPECT_EQ(frame.input.active_input_chords.front(), accepted_chord);

  QKeyEvent shift_release(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier,
                          0x2aU, 0x10U, 0U);
  QApplication::sendEvent(&surface, &shift_release);
  QKeyEvent changed_repeat(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier,
                           0x04U, 0x33U, 0U, QStringLiteral("3"));
  QApplication::sendEvent(&surface, &changed_repeat);
  frame = surface.take_input_frame(0.0F, true);
  EXPECT_TRUE(frame.input.pressed_input_chords.empty());
  EXPECT_TRUE(frame.input.released_input_chords.empty());
  ASSERT_EQ(frame.input.active_input_chords.size(), 1U);
  EXPECT_EQ(frame.input.active_input_chords.front(), accepted_chord);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_3, Qt::NoModifier,
                    0x04U, 0x33U, 0U, QStringLiteral("3"));
  QApplication::sendEvent(&surface, &release);
  frame = surface.take_input_frame(0.0F, true);
  ASSERT_EQ(frame.input.released_input_chords.size(), 1U);
  EXPECT_EQ(frame.input.released_input_chords.front(), accepted_chord);
  EXPECT_TRUE(frame.input.active_input_chords.empty());
#endif
}

TEST(QtViewportSurfaceInputTests,
     NativeKeyReleaseRemainsAbandonedAfterGlyphAndFocusChanges) {
#ifndef Q_OS_WIN
  GTEST_SKIP() << "Windows native key metadata supplies layout-independent keys.";
#else
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-abandoned-native-key-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  for (const bool focus_loss : {false, true}) {
    SCOPED_TRACE(focus_loss);
    QFocusEvent focus(QEvent::FocusIn);
    QApplication::sendEvent(&surface, &focus);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_sterling, Qt::ShiftModifier,
                    0x04U, 0x33U, 0U, QString(QChar(0x00a3)));
    QApplication::sendEvent(&surface, &press);
    const auto pressed = surface.take_input_frame(0.0F, true);
    ASSERT_EQ(pressed.input.pressed_input_chords.size(), 1U);
    if (focus_loss) {
      QFocusEvent lost(QEvent::FocusOut);
      QApplication::sendEvent(&surface, &lost);
      const auto interrupted = surface.take_input_frame(0.0F, false);
      ASSERT_EQ(interrupted.input.released_input_chords.size(), 1U);
      EXPECT_EQ(interrupted.input.released_input_chords.front(),
                pressed.input.pressed_input_chords.front());
    } else {
      surface.abandon_input();
    }
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_3, Qt::NoModifier,
                      0x04U, 0x33U, 0U, QStringLiteral("3"));
    QApplication::sendEvent(&surface, &release);
    const auto trailing = surface.take_input_frame(0.0F, true);
    EXPECT_TRUE(trailing.input.pressed_input_chords.empty());
    EXPECT_TRUE(trailing.input.released_input_chords.empty());
    EXPECT_TRUE(trailing.input.active_input_chords.empty());
    QKeyEvent fresh_press(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier,
                          0x04U, 0x33U, 0U, QStringLiteral("3"));
    QApplication::sendEvent(&surface, &fresh_press);
    QApplication::sendEvent(&surface, &release);
    const auto fresh = surface.take_input_frame(0.0F, true);
    ASSERT_EQ(fresh.input.pressed_input_chords.size(), 1U);
    ASSERT_EQ(fresh.input.released_input_chords.size(), 1U);
    EXPECT_FALSE(fresh.input.pressed_input_chords.front().shift);
    EXPECT_EQ(fresh.input.pressed_input_chords.front(),
              fresh.input.released_input_chords.front());
  }
#endif
}

TEST(QtViewportSurfaceInputTests,
     NativeKeypadReleaseMatchesItsPressAfterNumLockChanges) {
#ifndef Q_OS_WIN
  GTEST_SKIP() << "Windows native key metadata supplies keypad scan identities.";
#else
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-keypad-numlock-lifecycle-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  quader::QtViewportSurface surface(quader::ViewportPaneId::Perspective);
  QKeyEvent press(QEvent::KeyPress, Qt::Key_1, Qt::KeypadModifier,
                  0x4fU, 0x61U, 0U, QStringLiteral("1"));
  QApplication::sendEvent(&surface, &press);
  const auto held = surface.take_input_frame(0.0F, true);
  ASSERT_EQ(held.input.pressed_input_chords.size(), 1U);
  EXPECT_EQ(held.input.pressed_input_chords.front().key, QuaderKeyCode::Numpad1);
  QKeyEvent release(QEvent::KeyRelease, Qt::Key_End, Qt::NoModifier,
                    0x4fU, 0x23U, 0U);
  QApplication::sendEvent(&surface, &release);
  const auto released = surface.take_input_frame(0.0F, true);
  ASSERT_EQ(released.input.released_input_chords.size(), 1U);
  EXPECT_EQ(released.input.released_input_chords.front(),
            held.input.pressed_input_chords.front());
  EXPECT_TRUE(released.input.active_input_chords.empty());
#endif
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
  surface.resize(800, 600);
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
  resize_viewport_surface(panel, *top_surface, {800, 600});
  const QPointF center{400.0, 300.0};
  const QPointF center_global{500.0, 400.0};
  QEnterEvent top_enter(center, center, center_global);
  send_visible_viewport_event(top_surface, &top_enter);
  QFocusEvent top_focus(QEvent::FocusIn);
  send_visible_viewport_event(top_surface, &top_focus);

  const auto process_first = [&]() {
    panel.process_input(
        editor, first_session.scene(), first_session.assets(),
        first_session.modeling(), {}, first_session.native_tools(), nullptr,
        1.0 / 60.0);
  };
  process_first();
  QMouseEvent top_press(QEvent::MouseButtonPress, center, center_global,
                        Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(top_surface, &top_press);
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
  resize_viewport_surface(panel, *perspective_surface, {800, 600});
  QEnterEvent perspective_enter(center, center, center_global);
  send_visible_viewport_event(perspective_surface, &perspective_enter);
  QFocusEvent perspective_focus(QEvent::FocusIn);
  send_visible_viewport_event(perspective_surface, &perspective_focus);
  process_first();
  QMouseEvent perspective_press(
      QEvent::MouseButtonPress, center, center_global, Qt::MiddleButton,
      Qt::MiddleButton, Qt::NoModifier);
  send_visible_viewport_event(perspective_surface, &perspective_press);
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
  resize_viewport_surface(panel, *surface, {800, 600});
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);

  const auto process_bound_scene = [&]() {
    panel.process_input(
        editor, bound_scene->scene(), bound_scene->assets(),
        bound_scene->modeling(), {}, bound_scene->native_tools(),
        &bound_scene->point_placement(), 1.0 / 60.0);
  };
  process_bound_scene();

  QKeyEvent close_press(QEvent::KeyPress, Qt::Key_W, Qt::ControlModifier,
                        QStringLiteral("w"));
  send_visible_viewport_event(surface, &close_press);
  QKeyEvent select_all_press(QEvent::KeyPress, Qt::Key_A,
                             Qt::ControlModifier, QStringLiteral("a"));
  send_visible_viewport_event(surface, &select_all_press);
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
  send_visible_viewport_event(surface, &close_release);
  QKeyEvent select_all_release(QEvent::KeyRelease, Qt::Key_A,
                               Qt::ControlModifier, QStringLiteral("a"));
  send_visible_viewport_event(surface, &select_all_release);
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
  surface.resize(800, 600);
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
  resize_viewport_surface(panel, *surface, {800, 600});
  const QPointF local{400.0, 300.0};
  const QPointF global{500.0, 400.0};
  QEnterEvent enter(local, local, global);
  send_visible_viewport_event(surface, &enter);
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);

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
  send_visible_viewport_event(surface, &blocked_out);
  send_visible_viewport_event(surface, &reverse_in);
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
  send_visible_viewport_event(surface, &blocked_in);
  send_visible_viewport_event(surface, &reverse_out);
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
  resize_viewport_surface(panel, *surface, {800, 600});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
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
    send_visible_viewport_event(surface, &event);
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
  send_visible_viewport_event(surface, &move);
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
      send_visible_viewport_event(surface, &prefix);
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

TEST(QtViewportSurfaceInputTests,
     QuadSphereWheelChangesSubdivisionAndConsumesLimitsWithoutNavigation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-quad-sphere-wheel-panel-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kPrimitiveTypeSetting,
      static_cast<double>(quader::PrimitiveCreationKind::Sphere)).ok());
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kSphereTypeSetting, 1.0).ok());
  ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
      quader::kSphereSubdivisionSetting, 1.0).ok());
  quader::ModelingBoxPreview preview;
  preview.ok = preview.valid = true;
  preview.drag_state = quader::ModelingBoxDragState::Height;
  preview.corners = {quader::QVec3{-2, -2, -2}, quader::QVec3{-2, -2, 2},
                     quader::QVec3{2, -2, 2}, quader::QVec3{2, -2, -2},
                     quader::QVec3{-2, 2, -2}, quader::QVec3{-2, 2, 2},
                     quader::QVec3{2, 2, 2}, quader::QVec3{2, 2, -2}};
  preview.corner_count = 8;
  ASSERT_TRUE(session.primitive_creation().accept_preview(preview, "Quad Sphere").ok);
  const auto original = editor.tools().live_primitive_creation();
  ASSERT_TRUE(original);
  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(panel, *surface, {800, 600});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  ASSERT_TRUE(camera.restore_perspective_pose(
      {.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  const auto process = [&] {
    panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  process();
  quader::PrimitiveCreationInteraction projection;
  const auto wheel = [&](int delta, bool pixel = false,
                          Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const auto &controls = projection.project(
        quader::ViewportPrimitiveCreationApiAdapter::from_api(
            editor.tools().live_primitive_creation()), camera,
        panel.viewport_raster_scale(quader::ViewportPaneId::Perspective));
    ASSERT_TRUE(controls.controls[6].visible);
    EXPECT_FALSE(controls.controls[7].visible);
    const auto pixels = controls.controls[6].screen_position;
    const QPointF point{pixels.x / surface->devicePixelRatio(),
                        pixels.y / surface->devicePixelRatio()};
    QWheelEvent event(point, point, pixel ? QPoint{0, delta} : QPoint{},
                      pixel ? QPoint{} : QPoint{0, delta}, Qt::NoButton,
                      modifiers, Qt::NoScrollPhase, false);
    send_visible_viewport_event(surface, &event);
  };
  const auto subdivision = [&] {
    return editor.tools().live_primitive_creation()->sphere_subdivision;
  };
  const auto distance = camera.state().distance;
  const auto target = camera.state().target;
  wheel(-120);
  wheel(120);
  // Wheel hit tests retain each event's button position even after the pointer leaves it.
  QMouseEvent move(QEvent::MouseMove, {12, 12}, {12, 12},
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &move);
  process();
  EXPECT_EQ(subdivision(), 2);

  wheel(-20, true, Qt::ControlModifier);
  process();
  EXPECT_EQ(subdivision(), 2);
  wheel(-20, true, Qt::ControlModifier);
  process();
  EXPECT_EQ(subdivision(), 1);
  wheel(-40, true, Qt::ControlModifier);
  process();
  EXPECT_EQ(subdivision(), 1);
  wheel(120);
  process();
  EXPECT_EQ(subdivision(), 2);
  EXPECT_FLOAT_EQ(camera.state().distance, distance);
  EXPECT_FLOAT_EQ(quader::length_squared(camera.state().target - target), 0.0f);
  const auto current = editor.tools().live_primitive_creation();
  ASSERT_TRUE(current);
  EXPECT_EQ(current->radial_segments, original->radial_segments);
  EXPECT_EQ(current->rings, original->rings);
  EXPECT_EQ(current->object_id, original->object_id);
  EXPECT_FLOAT_EQ(quader::length_squared(current->local_min - original->local_min), 0.0f);
  EXPECT_FLOAT_EQ(quader::length_squared(current->local_max - original->local_max), 0.0f);
  EXPECT_FALSE(current->resizing);
  EXPECT_EQ(session.primitive_creation().shape_count(), 1);
}

TEST(QtViewportSurfaceInputTests,
     KnifeStartsWithinEightLogicalPixelsWithASmallPersistedPickRadius) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-knife-start-tolerance-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Knife tolerance box");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Back).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Knife).ok());

  quader::ViewportSettings settings = quader::ViewportSettings::defaults();
  ASSERT_TRUE(settings.set_float_value(
      quader::ViewportFloatSettingId::PickEdgeRadius, 0.1f));
  quader::QtViewportPanel panel;
  panel.set_viewport_settings(settings);
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(panel, *surface, {800, 600});
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &enter);
  send_visible_viewport_event(surface, &focus);
  auto &camera = session.viewport_workspace().camera(
      quader::ViewportPaneId::Perspective);
  camera.focus_origin(5.0f);

  const auto process = [&] {
    panel.process_input(
        editor, session.scene(), session.assets(), session.modeling(), {},
        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
  };
  process();

  const auto edge_middle = camera.project_world_point({1.0f, 0.0f, 1.0f});
  const auto face_center = camera.project_world_point({0.0f, 0.0f, 1.0f});
  ASSERT_TRUE(edge_middle.visible);
  ASSERT_TRUE(face_center.visible);
  const quader::QVec2 toward_face = face_center.position - edge_middle.position;
  const float direction_length = std::sqrt(
      toward_face.x * toward_face.x + toward_face.y * toward_face.y);
  ASSERT_GT(direction_length, 0.0f);
  constexpr float kOffsetLogicalPixels = 6.0f;
  const float physical_offset =
      kOffsetLogicalPixels *
      panel.viewport_raster_scale(quader::ViewportPaneId::Perspective);
  const quader::QVec2 offset_position =
      edge_middle.position + toward_face * (physical_offset / direction_length);
  const qreal device_pixel_ratio = surface->devicePixelRatio();
  const QPointF local{offset_position.x / device_pixel_ratio,
                      offset_position.y / device_pixel_ratio};
  const QPointF global{surface->mapToGlobal(local.toPoint())};

  QMouseEvent move(QEvent::MouseMove, local, global, Qt::NoButton,
                   Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &move);
  process();
  ASSERT_TRUE(session.modeling().tool_status().knife_has_current)
      << "The production 0.1 pick radius must retain an eight-logical-pixel "
         "Knife seed tolerance";

  QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &press);
  process();
  EXPECT_TRUE(session.modeling().tool_status().knife_active);
  EXPECT_TRUE(session.modeling().tool_status().knife_has_active_stroke);

  QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                      Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &release);
  process();
  EXPECT_TRUE(session.modeling().tool_status().knife_active);

  const auto history_before_outside = editor.history().snapshot();
  QEvent leave(QEvent::Leave);
  QFocusEvent blur(QEvent::FocusOut);
  send_visible_viewport_event(surface, &leave);
  send_visible_viewport_event(surface, &blur);
  QWidget outside;
  QMouseEvent outside_press(QEvent::MouseButtonPress, {10, 10}, {-100, -100},
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QMouseEvent outside_release(QEvent::MouseButtonRelease, {10, 10}, {-100, -100},
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QApplication::sendEvent(&outside, &outside_press);
  QApplication::sendEvent(&outside, &outside_release);
  process();
  EXPECT_TRUE(session.modeling().tool_status().knife_has_active_stroke)
      << "Panel clicks must leave the draft for the requested command to settle";
  expect_history_unchanged(editor.history().snapshot(), history_before_outside);
  ASSERT_TRUE(editor.tools().cancel_modal_tool().ok());
  EXPECT_FALSE(session.modeling().tool_status().knife_has_active_stroke);
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
    resize_viewport_surface(panel, *surface, {800, 600});
    QFocusEvent focus(QEvent::FocusIn);
    send_visible_viewport_event(surface, &focus);
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
    send_visible_viewport_event(surface, &press);
    process();
    QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                         Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    send_visible_viewport_event(surface, &release);
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

TEST(QtViewportSurfaceInputTests,
     PrimitiveMaterialIsSampledAtFootprintAcceptanceAndRetainedDuringEdits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-primitive-browser-material-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  for (const auto kind : {quader::PrimitiveCreationKind::Box,
                          quader::PrimitiveCreationKind::Cylinder,
                          quader::PrimitiveCreationKind::Cone,
                          quader::PrimitiveCreationKind::Sphere,
                          quader::PrimitiveCreationKind::Torus}) {
    SCOPED_TRACE(static_cast<int>(kind));
    for (const bool has_browser_selection : {true, false}) {
      SCOPED_TRACE(has_browser_selection);
      quader::SceneSession session;
      quader::QuaderEditorApi editor(
          quader::QuaderApiSessionInternalAccess::from_scene_session(session));
      const auto fallback = editor.assets().add_material(
          quader::QuaderMaterialArgs{.name = "Fallback primitive material"});
      const auto selected = editor.assets().add_material(
          quader::QuaderMaterialArgs{.name = "Selected primitive material"});
      ASSERT_TRUE(fallback.ok()) << fallback.error_message();
      ASSERT_TRUE(selected.ok()) << selected.error_message();
      ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
      ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
      ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
          quader::kPrimitiveTypeSetting, static_cast<double>(kind)).ok());

      quader::QtViewportPanel panel;
      panel.attach_workspace(session.viewport_workspace());
      auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
      ASSERT_NE(surface, nullptr);
      resize_viewport_surface(panel, *surface, {800, 600});
      QFocusEvent focus(QEvent::FocusIn);
      send_visible_viewport_event(surface, &focus);
      auto &camera = session.viewport_workspace().camera(
          quader::ViewportPaneId::Perspective);
      ASSERT_TRUE(camera.restore_perspective_pose(
          {.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
      auto browser_selection = quader::QuaderMaterialReference::embedded(
          fallback.value().id);
      int material_queries = 0;
      panel.set_primitive_creation_material_provider([&] {
        ++material_queries;
        return browser_selection;
      });
      const auto process = [&] {
        panel.process_input(editor, session.scene(), session.assets(),
            session.modeling(), fallback.value().id, session.native_tools(),
            &session.point_placement(), 1.0 / 60.0);
      };
      for (int frame = 0; frame < 8; ++frame) {
        process();
      }
      EXPECT_EQ(material_queries, 0);
      QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                         Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
      send_visible_viewport_event(surface, &press);
      process();
      EXPECT_EQ(material_queries, 0);

      // A browser choice made after the tool starts belongs to this primitive.
      browser_selection = has_browser_selection
          ? quader::QuaderMaterialReference::embedded(selected.value().id)
          : quader::QuaderMaterialReference{};
      QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
      send_visible_viewport_event(surface, &release);
      process();
      ASSERT_TRUE(editor.tools().live_primitive_creation());
      ASSERT_EQ(material_queries, 1);
      const auto entity = session.scene().primary_selection();
      const auto expect_material = [&] {
        const auto *material = session.scene().material(entity);
        if (has_browser_selection) {
          ASSERT_NE(material, nullptr);
          EXPECT_EQ(material->material, selected.value().id);
        } else {
          // Scene-only fixtures have no project Default Material binding.
          EXPECT_EQ(material, nullptr);
        }
      };
      ASSERT_NO_FATAL_FAILURE(expect_material());

      browser_selection = quader::QuaderMaterialReference::embedded(
          has_browser_selection ? fallback.value().id : selected.value().id);
      ASSERT_TRUE(editor.tools().set_setting(quader::kCreatePrimitiveToolId,
          quader::kPrimitiveFlipFacesSetting, true).ok());
      for (int frame = 0; frame < 8; ++frame) {
        process();
      }
      EXPECT_EQ(material_queries, 1);
      ASSERT_NO_FATAL_FAILURE(expect_material());
      ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
      EXPECT_EQ(material_queries, 1);
    }
  }
}

TEST(QtViewportSurfaceInputTests,
     PrimitiveMaterialProviderAbandonmentInvalidatesPendingAcceptance) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-primitive-material-binding-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  quader::QtViewportPanel panel;
  panel.attach_workspace(session.viewport_workspace());
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  resize_viewport_surface(panel, *surface, {800, 600});
  QFocusEvent focus(QEvent::FocusIn);
  send_visible_viewport_event(surface, &focus);
  auto &camera = session.viewport_workspace().camera(
      quader::ViewportPaneId::Perspective);
  ASSERT_TRUE(camera.restore_perspective_pose(
      {.eye = {8, 7, 11}, .target = {}, .up = {0, 1, 0}}));
  int material_queries = 0;
  panel.set_primitive_creation_material_provider([&] {
    ++material_queries;
    panel.abandon_interactions();
    return quader::QuaderMaterialReference{};
  });
  const auto process = [&] {
    panel.process_input(editor, session.scene(), session.assets(),
        session.modeling(), {}, session.native_tools(),
        &session.point_placement(), 1.0 / 60.0);
  };
  process();
  QMouseEvent press(QEvent::MouseButtonPress, {300, 300}, {300, 300},
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &press);
  process();
  QMouseEvent release(QEvent::MouseButtonRelease, {500, 430}, {500, 430},
                       Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  send_visible_viewport_event(surface, &release);
  process();
  EXPECT_EQ(material_queries, 1);
  EXPECT_TRUE(session.modeling().objects().empty());
  EXPECT_EQ(session.primitive_creation().shape_count(), 0);
  process();
  EXPECT_EQ(material_queries, 1);
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
}
