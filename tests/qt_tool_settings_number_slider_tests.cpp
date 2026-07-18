////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_tool_settings_window.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>

namespace {

void send_mouse_event(QWidget &target, QEvent::Type type, QPoint position,
                      Qt::MouseButton button, Qt::MouseButtons buttons) {
  const QPoint global = target.mapToGlobal(position);
  QMouseEvent event(type, QPointF(position), QPointF(global), button, buttons,
                    Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
}

void send_key_event(QWidget &target, int key) {
  QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
  QCoreApplication::sendEvent(&target, &event);
}

quader::ModelingBoxPreview tool_settings_primitive_preview() {
  quader::ModelingBoxPreview preview;
  preview.ok = true;
  preview.valid = true;
  preview.drag_state = quader::ModelingBoxDragState::Height;
  preview.corners = {
      quader::QVec3{-1.0f, 0.0f, -1.0f},
      quader::QVec3{-1.0f, 0.0f, 1.0f},
      quader::QVec3{1.0f, 0.0f, 1.0f},
      quader::QVec3{1.0f, 0.0f, -1.0f},
      quader::QVec3{-1.0f, 1.0f, -1.0f},
      quader::QVec3{-1.0f, 1.0f, 1.0f},
      quader::QVec3{1.0f, 1.0f, 1.0f},
      quader::QVec3{1.0f, 1.0f, -1.0f},
  };
  preview.corner_count = preview.corners.size();
  return preview;
}

QRect slider_groove(const QSlider &slider) {
  QStyleOptionSlider option;
  option.initFrom(&slider);
  option.orientation = slider.orientation();
  option.minimum = slider.minimum();
  option.maximum = slider.maximum();
  option.sliderPosition = slider.sliderPosition();
  option.sliderValue = slider.value();
  return slider.style()->subControlRect(QStyle::CC_Slider, &option,
                                        QStyle::SC_SliderGroove, &slider);
}

QRect slider_handle(const QSlider &slider) {
  QStyleOptionSlider option;
  option.initFrom(&slider);
  option.orientation = slider.orientation();
  option.minimum = slider.minimum();
  option.maximum = slider.maximum();
  option.sliderPosition = slider.sliderPosition();
  option.sliderValue = slider.value();
  return slider.style()->subControlRect(QStyle::CC_Slider, &option,
                                        QStyle::SC_SliderHandle, &slider);
}

QPoint forward_drag_position(const QSlider &slider) {
  const QRect groove = slider_groove(slider);
  const QRect handle = slider_handle(slider);
  const int distance = std::max(4, groove.width() / 16);
  return {
      std::min(groove.right(), handle.center().x() + distance),
      groove.center().y(),
  };
}

const quader::QuaderToolSettingControl *find_setting(
    const quader::QuaderToolSettingsTarget &target, std::string_view id) {
  const auto found = std::find_if(
      target.controls.begin(), target.controls.end(),
      [id](const quader::QuaderToolSettingControl &control) {
        return control.id == id;
      });
  return found == target.controls.end() ? nullptr : &*found;
}

} // namespace

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
      quader::qt_tool_settings_number_slider_mapping(-2.5, 7.5, false, 0.01);

  EXPECT_FALSE(mapping.integer);
  EXPECT_EQ(mapping.curve,
            quader::QtToolSettingsNumberSliderCurve::Linear);
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
     WideNonnegativeRangesGivePracticalLowValuesUsableTravelAndPrecision) {
  const quader::QtToolSettingsNumberSliderMapping mapping =
      quader::qt_tool_settings_number_slider_mapping(0.0, 16384.0, false,
                                                      0.001);
  EXPECT_EQ(mapping.curve,
            quader::QtToolSettingsNumberSliderCurve::LogarithmicFromMinimum);
  const int practical_position =
      quader::qt_tool_settings_number_slider_position(mapping, 8.2765);
  EXPECT_GT(practical_position,
            quader::kQtToolSettingsFloatingSliderResolution / 10);
  EXPECT_LT(practical_position,
            quader::kQtToolSettingsFloatingSliderResolution / 2);

  const quader::QtToolSettingsNumberSliderMapping bevel_mapping =
      quader::qt_tool_settings_number_slider_mapping(0.0, 512.0, false,
                                                      0.001);
  EXPECT_GT(quader::qt_tool_settings_number_slider_position(bevel_mapping,
                                                             0.1),
            quader::kQtToolSettingsFloatingSliderResolution / 5);

  constexpr std::array<double, 8> kValues{0.0,    0.001, 0.25,    1.0,
                                          8.2765, 127.75, 8192.0, 16384.0};
  int previous_position = -1;

  for (double value : kValues) {
    const int position =
        quader::qt_tool_settings_number_slider_position(mapping, value);
    const double round_trip =
        quader::qt_tool_settings_number_slider_value(mapping, position);
    EXPECT_GT(position, previous_position);
    previous_position = position;
    const double tolerance = std::max(0.001, std::abs(value) * 0.0002);
    EXPECT_NEAR(round_trip, value, tolerance);
  }
}

TEST(QtToolSettingsNumberSliderTests,
     WideSignedRangesRemainMonotonicSymmetricAndZeroCentered) {
  const quader::QtToolSettingsNumberSliderMapping mapping =
      quader::qt_tool_settings_number_slider_mapping(-16384.0, 16384.0, false,
                                                      0.001);
  EXPECT_EQ(mapping.curve,
            quader::QtToolSettingsNumberSliderCurve::SymmetricLogarithmic);
  const int center = quader::kQtToolSettingsFloatingSliderResolution / 2;
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(mapping, 0.0),
            center);
  const int negative =
      quader::qt_tool_settings_number_slider_position(mapping, -8.2765);
  const int positive =
      quader::qt_tool_settings_number_slider_position(mapping, 8.2765);
  EXPECT_EQ(center - negative, positive - center);
  EXPECT_GT(positive - center,
            quader::kQtToolSettingsFloatingSliderResolution / 20);
  EXPECT_NEAR(quader::qt_tool_settings_number_slider_value(mapping, negative),
              -8.2765, 0.001);
  EXPECT_NEAR(quader::qt_tool_settings_number_slider_value(mapping, positive),
              8.2765, 0.001);
}

TEST(QtToolSettingsNumberSliderTests,
     DegenerateAndReversedRangesRemainDeterministic) {
  const quader::QtToolSettingsNumberSliderMapping reversed =
      quader::qt_tool_settings_number_slider_mapping(5.0, -5.0, false, 0.01);
  EXPECT_DOUBLE_EQ(reversed.value_minimum, -5.0);
  EXPECT_DOUBLE_EQ(reversed.value_maximum, 5.0);

  const quader::QtToolSettingsNumberSliderMapping degenerate =
      quader::qt_tool_settings_number_slider_mapping(3.0, 3.0, false, 0.01);
  EXPECT_EQ(degenerate.slider_minimum, 0);
  EXPECT_EQ(degenerate.slider_maximum, 0);
  EXPECT_EQ(quader::qt_tool_settings_number_slider_position(degenerate, 99.0),
            0);
  EXPECT_DOUBLE_EQ(quader::qt_tool_settings_number_slider_value(degenerate, 99),
                   3.0);
}

TEST(QtToolSettingsNumberSliderTests,
     GroovePressStartsContinuousTrackedScrubAndRetainsNativeHandleDrag) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-slider-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsNumberSlider slider(Qt::Horizontal);
  slider.setRange(0, 1000);
  slider.setValue(0);
  slider.setTracking(true);
  slider.resize(240, 32);
  slider.show();
  QCoreApplication::processEvents();

  int pressed = 0;
  int released = 0;
  int moved = 0;
  QObject::connect(&slider, &QSlider::sliderPressed,
                   [&pressed]() { ++pressed; });
  QObject::connect(&slider, &QSlider::sliderReleased,
                   [&released]() { ++released; });
  QObject::connect(&slider, &QSlider::sliderMoved,
                   [&moved](int) { ++moved; });

  const QRect groove = slider_groove(slider);
  ASSERT_FALSE(groove.isEmpty());
  const QPoint press_position(
      groove.left() + (groove.width() * 3) / 4, groove.center().y());
  send_mouse_event(slider, QEvent::MouseButtonPress, press_position,
                   Qt::LeftButton, Qt::LeftButton);
  EXPECT_TRUE(slider.isSliderDown());
  EXPECT_EQ(pressed, 1);
  EXPECT_GT(slider.value(), 600);
  const int pressed_value = slider.value();

  const QPoint move_position(groove.right(), groove.center().y());
  send_mouse_event(slider, QEvent::MouseMove, move_position, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_TRUE(slider.isSliderDown());
  EXPECT_GT(slider.value(), pressed_value);
  EXPECT_GT(moved, 1);

  send_mouse_event(slider, QEvent::MouseButtonRelease, move_position,
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(slider.isSliderDown());
  EXPECT_EQ(released, 1);
  EXPECT_EQ(slider.value(), slider.maximum());

  slider.setValue(500);
  const QRect handle = slider_handle(slider);
  ASSERT_FALSE(handle.isEmpty());
  send_mouse_event(slider, QEvent::MouseButtonPress, handle.center(),
                   Qt::LeftButton, Qt::LeftButton);
  EXPECT_TRUE(slider.isSliderDown());
  EXPECT_EQ(pressed, 2);
  send_mouse_event(slider, QEvent::MouseMove,
                   QPoint(groove.left(), groove.center().y()), Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_LT(slider.value(), 100);
  send_mouse_event(slider, QEvent::MouseButtonRelease,
                   QPoint(groove.left(), groove.center().y()),
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(slider.isSliderDown());
  EXPECT_EQ(released, 2);

  slider.setInvertedAppearance(true);
  slider.setValue(500);
  send_mouse_event(slider, QEvent::MouseButtonPress, press_position,
                   Qt::LeftButton, Qt::LeftButton);
  EXPECT_TRUE(slider.isSliderDown());
  EXPECT_LT(slider.value(), 400);
  send_mouse_event(slider, QEvent::MouseButtonRelease, press_position,
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(slider.isSliderDown());
  EXPECT_EQ(pressed, 3);
  EXPECT_EQ(released, 3);

  slider.hide();
}

TEST(QtToolSettingsNumberSliderTests,
     DynamicRadialBoundsUpdateInPlaceWithoutDestroyingActiveEditors) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
          "Slider Retention Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::RadialArray).ok());

  quader::QtToolSettingsWindow window;
  window.bind(editor);
  QCoreApplication::processEvents();
  auto *copies = dynamic_cast<quader::QtToolSettingsNumberSlider *>(
      window.findChild<QSlider *>(QStringLiteral(
          "tool_settings.control.radial_array.copies.slider")));
  auto *repeats = dynamic_cast<quader::QtToolSettingsNumberSlider *>(
      window.findChild<QSlider *>(QStringLiteral(
          "tool_settings.control.radial_array.repeats.slider")));
  ASSERT_NE(copies, nullptr);
  ASSERT_NE(repeats, nullptr);
  QPointer<quader::QtToolSettingsNumberSlider> retained_copies(copies);
  QPointer<quader::QtToolSettingsNumberSlider> retained_repeats(repeats);

  copies->setSliderDown(true);
  ASSERT_TRUE(editor.tools()
                  .set_setting("radial_array", "radial_array.copies", 16.0)
                  .ok());
  window.refresh();
  EXPECT_EQ(window.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.radial_array.copies.slider")),
            retained_copies.data());
  EXPECT_EQ(window.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.radial_array.repeats.slider")),
            retained_repeats.data());
  EXPECT_TRUE(retained_copies->isSliderDown());
  EXPECT_EQ(retained_repeats->maximum(), 64);
  retained_copies->setSliderDown(false);

  auto *copies_spin = window.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.radial_array.copies"));
  ASSERT_NE(copies_spin, nullptr);
  window.activateWindow();
  copies_spin->setFocus(Qt::MouseFocusReason);
  QCoreApplication::processEvents();
  ASSERT_TRUE(copies_spin->hasFocus() ||
              (QApplication::focusWidget() != nullptr &&
               copies_spin->isAncestorOf(QApplication::focusWidget())));
  {
    const QSignalBlocker blocker(copies_spin);
    copies_spin->setValue(12);
  }
  window.refresh();
  EXPECT_EQ(copies_spin->value(), 12);
  copies_spin->clearFocus();
  window.refresh();
  EXPECT_EQ(copies_spin->value(), 16);

  window.unbind();
  window.hide();
}

TEST(QtToolSettingsNumberSliderTests,
     PrimitiveEnterEditsAndEscapeRemainsSemanticAcrossDynamicSettingsEditors) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-primitive-tool-settings-key-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  quader::ViewportToolId active_tool =
      quader::ViewportToolId::CreatePrimitive;
  quader::QtCommandEventFilter command_filter(
      editor, [&active_tool]() {
        return quader::ViewportToolState{.active_tool = active_tool};
      });
  application.installEventFilter(&command_filter);

  ASSERT_TRUE(editor.tools().activate(active_tool).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(tool_settings_primitive_preview(),
                                  "Focused Settings Box")
                  .ok);
  quader::QtToolSettingsWindow window;
  window.bind(editor);
  window.show();
  window.activateWindow();
  QCoreApplication::processEvents();

  auto *subdivisions = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.box.subdivisions_x"));
  ASSERT_NE(subdivisions, nullptr);
  subdivisions->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  QLineEdit *subdivision_editor = subdivisions->findChild<QLineEdit *>();
  ASSERT_NE(subdivision_editor, nullptr);
  subdivision_editor->setText(QStringLiteral("5"));
  send_key_event(*subdivision_editor, Qt::Key_Return);

  EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x, 5);
  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  send_key_event(*subdivision_editor, Qt::Key_Space);
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(editor.history().can_undo());

  active_tool = quader::ViewportToolId::CreatePrimitive;
  ASSERT_TRUE(editor.tools().activate(active_tool).ok());
  window.refresh();
  QCoreApplication::processEvents();
  auto *primitive_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.type"));
  ASSERT_NE(primitive_type, nullptr);
  ASSERT_EQ(primitive_type->count(), 5);
  EXPECT_EQ(primitive_type->currentData().toString(), QStringLiteral("box"));
  const int cone_index = primitive_type->findData(QStringLiteral("cone"));
  ASSERT_GE(cone_index, 0);
  primitive_type->setCurrentIndex(cone_index);
  QCoreApplication::processEvents();
  EXPECT_EQ(editor.tools().active_primitive_type(), "cone");
  ASSERT_NE(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.cone.side_count")),
            nullptr);

  primitive_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.type"));
  ASSERT_NE(primitive_type, nullptr);
  const int torus_index = primitive_type->findData(QStringLiteral("torus"));
  ASSERT_GE(torus_index, 0);
  primitive_type->setCurrentIndex(torus_index);
  QCoreApplication::processEvents();
  EXPECT_EQ(editor.tools().active_primitive_type(), "torus");
  ASSERT_NE(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.torus.major_segments")),
            nullptr);
  ASSERT_NE(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.torus.minor_segments")),
            nullptr);
  ASSERT_NE(window.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.torus.tube_radius_ratio")),
            nullptr);

  primitive_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.type"));
  ASSERT_NE(primitive_type, nullptr);
  const int cylinder_index =
      primitive_type->findData(QStringLiteral("cylinder"));
  ASSERT_GE(cylinder_index, 0);
  primitive_type->setCurrentIndex(cylinder_index);
  QCoreApplication::processEvents();
  EXPECT_EQ(editor.tools().active_primitive_type(), "cylinder");

  auto *sides = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.cylinder.side_count"));
  ASSERT_NE(sides, nullptr);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(tool_settings_primitive_preview(),
                                  "Focused Settings Cylinder")
                  .ok);
  primitive_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.type"));
  ASSERT_NE(primitive_type, nullptr);
  primitive_type->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_event(*primitive_type, Qt::Key_Return);

  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  EXPECT_TRUE(editor.history().can_undo());

  active_tool = quader::ViewportToolId::CreatePrimitive;
  ASSERT_TRUE(editor.tools().activate(active_tool).ok());
  window.refresh();
  QCoreApplication::processEvents();
  primitive_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.type"));
  ASSERT_NE(primitive_type, nullptr);
  EXPECT_EQ(primitive_type->currentData().toString(),
            QStringLiteral("cylinder"));
  primitive_type->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_event(*primitive_type, Qt::Key_Escape);

  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(editor.history().can_undo());

  application.removeEventFilter(&command_filter);
  window.unbind();
  window.hide();
}

TEST(QtToolSettingsNumberSliderTests,
     BevelPointerMovesCoalesceAndFrameSyncRetainsEditors) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-bevel-slider-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> source =
      editor.modeling().create_box(
          quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                               .max = {1.0f, 1.0f, 1.0f}},
          "Bevel Slider Dispatch Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_all().ok());
  ASSERT_TRUE(editor.commands()
                  .execute(editor.commands().command_id_for_modeling_action(
                      "bevel_edges"))
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("edge_bevel",
                               "operation.edge_bevel.width", 0.1)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("edge_bevel",
                               "operation.edge_bevel.segments", 1.0)
                  .ok());

  quader::QtToolSettingsWindow window;
  window.bind(editor);
  QCoreApplication::processEvents();

  auto *width_slider = window.findChild<QSlider *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width.slider"));
  auto *width_spin = window.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width"));
  ASSERT_NE(width_slider, nullptr);
  ASSERT_NE(width_spin, nullptr);
  ASSERT_FALSE(slider_groove(*width_slider).isEmpty());
  ASSERT_FALSE(slider_handle(*width_slider).isEmpty());
  QPointer<QSlider> retained_width_slider(width_slider);
  QPointer<QDoubleSpinBox> retained_width_spin(width_spin);
  int width_slider_changes = 0;
  int width_slider_moves = 0;
  int width_spin_changes = 0;
  int width_slider_destroys = 0;
  QObject::connect(width_slider, &QSlider::valueChanged,
                   [&width_slider_changes](int) { ++width_slider_changes; });
  QObject::connect(width_slider, &QSlider::sliderMoved,
                   [&width_slider_moves](int) { ++width_slider_moves; });
  QObject::connect(width_spin, &QDoubleSpinBox::valueChanged,
                   [&width_spin_changes](double) { ++width_spin_changes; });
  QObject::connect(width_slider, &QObject::destroyed,
                   [&width_slider_destroys]() { ++width_slider_destroys; });

  const QPoint width_press = slider_handle(*width_slider).center();
  const QPoint width_move = forward_drag_position(*width_slider);
  ASSERT_NE(width_move.x(), width_press.x());
  send_mouse_event(*width_slider, QEvent::MouseButtonPress, width_press,
                   Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(width_slider->isSliderDown());
  width_slider_changes = 0;
  width_slider_moves = 0;
  width_spin_changes = 0;
  const quader::QuaderRevisionStamp width_revisions_before =
      editor.revisions();
  const auto width_start = std::chrono::steady_clock::now();
  send_mouse_event(*width_slider, QEvent::MouseMove, width_move,
                   Qt::NoButton, Qt::LeftButton);
  const auto width_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                width_start);

  EXPECT_EQ(width_slider_moves, 1);
  EXPECT_EQ(width_slider_changes, 1);
  EXPECT_EQ(width_spin_changes, 0);
  EXPECT_EQ(width_slider_destroys, 0);
  EXPECT_EQ(editor.revisions().asset_revision,
            width_revisions_before.asset_revision);
  const double pending_width = width_spin->value();
  const quader::QuaderToolSettingsTarget pending_width_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *pending_width_setting =
      find_setting(pending_width_settings, "operation.edge_bevel.width");
  ASSERT_NE(pending_width_setting, nullptr);
  EXPECT_DOUBLE_EQ(pending_width_setting->number_value, 0.1);
  EXPECT_NE(pending_width_setting->number_value, pending_width);

  window.refresh();
  EXPECT_EQ(width_slider_changes, 1);
  EXPECT_EQ(width_spin_changes, 0);
  EXPECT_EQ(width_slider_destroys, 0);
  EXPECT_EQ(window.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.width.slider")),
            retained_width_slider.data());
  EXPECT_EQ(window.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.width")),
            retained_width_spin.data());
  EXPECT_TRUE(width_slider->isSliderDown());
  EXPECT_DOUBLE_EQ(width_spin->value(), pending_width);
  const auto width_release_start = std::chrono::steady_clock::now();
  send_mouse_event(*width_slider, QEvent::MouseButtonRelease, width_move,
                   Qt::LeftButton, Qt::NoButton);
  const auto width_release_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                width_release_start);
  EXPECT_FALSE(width_slider->isSliderDown());
  EXPECT_EQ(width_slider_changes, 1);
  EXPECT_EQ(width_spin_changes, 0);
  EXPECT_GT(editor.revisions().asset_revision,
            width_revisions_before.asset_revision);
  const quader::QuaderToolSettingsTarget applied_width_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *applied_width_setting =
      find_setting(applied_width_settings, "operation.edge_bevel.width");
  ASSERT_NE(applied_width_setting, nullptr);
  EXPECT_DOUBLE_EQ(applied_width_setting->number_value, pending_width);

  auto *segments_slider = window.findChild<QSlider *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.segments.slider"));
  auto *segments_spin = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.segments"));
  ASSERT_NE(segments_slider, nullptr);
  ASSERT_NE(segments_spin, nullptr);
  ASSERT_FALSE(slider_groove(*segments_slider).isEmpty());
  ASSERT_FALSE(slider_handle(*segments_slider).isEmpty());
  QPointer<QSlider> retained_segments_slider(segments_slider);
  QPointer<QSpinBox> retained_segments_spin(segments_spin);
  int segments_slider_changes = 0;
  int segments_slider_moves = 0;
  int segments_spin_changes = 0;
  int segments_slider_destroys = 0;
  QObject::connect(
      segments_slider, &QSlider::valueChanged,
      [&segments_slider_changes](int) { ++segments_slider_changes; });
  QObject::connect(segments_slider, &QSlider::sliderMoved,
                   [&segments_slider_moves](int) { ++segments_slider_moves; });
  QObject::connect(segments_spin, &QSpinBox::valueChanged,
                   [&segments_spin_changes](int) { ++segments_spin_changes; });
  QObject::connect(
      segments_slider, &QObject::destroyed,
      [&segments_slider_destroys]() { ++segments_slider_destroys; });

  const QRect segments_groove = slider_groove(*segments_slider);
  const QPoint segments_press = slider_handle(*segments_slider).center();
  const int segments_delta = std::max(4, segments_groove.width() / 16);
  const std::array<QPoint, 3> segments_moves{
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta),
             segments_groove.center().y()),
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta * 2),
             segments_groove.center().y()),
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta * 3),
             segments_groove.center().y()),
  };
  ASSERT_NE(segments_moves.front().x(), segments_press.x());
  ASSERT_LT(segments_moves.front().x(), segments_moves.back().x());
  send_mouse_event(*segments_slider, QEvent::MouseButtonPress, segments_press,
                   Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(segments_slider->isSliderDown());
  segments_slider_changes = 0;
  segments_slider_moves = 0;
  segments_spin_changes = 0;
  const quader::QuaderRevisionStamp segments_revisions_before =
      editor.revisions();
  const auto segments_start = std::chrono::steady_clock::now();
  for (const QPoint move : segments_moves) {
    send_mouse_event(*segments_slider, QEvent::MouseMove, move,
                     Qt::NoButton, Qt::LeftButton);
  }
  const auto segments_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                segments_start);

  EXPECT_EQ(segments_slider_moves, 3);
  EXPECT_EQ(segments_slider_changes, 3);
  EXPECT_EQ(segments_spin_changes, 0);
  EXPECT_EQ(segments_slider_destroys, 0);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_before.asset_revision);
  const int pending_segments = segments_spin->value();
  const quader::QuaderToolSettingsTarget pending_segments_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *pending_segments_setting =
      find_setting(pending_segments_settings,
                   "operation.edge_bevel.segments");
  ASSERT_NE(pending_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(pending_segments_setting->number_value, 1.0);
  EXPECT_NE(pending_segments_setting->number_value,
            static_cast<double>(pending_segments));

  window.refresh();
  EXPECT_EQ(segments_slider_changes, 3);
  EXPECT_EQ(segments_spin_changes, 0);
  EXPECT_EQ(segments_slider_destroys, 0);
  EXPECT_EQ(window.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.segments.slider")),
            retained_segments_slider.data());
  EXPECT_EQ(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.segments")),
            retained_segments_spin.data());
  EXPECT_TRUE(segments_slider->isSliderDown());
  EXPECT_EQ(segments_spin->value(), pending_segments);
  QEventLoop live_update_loop;
  QTimer::singleShot(50, &live_update_loop, &QEventLoop::quit);
  live_update_loop.exec();
  const quader::QuaderRevisionStamp segments_revisions_after_live_update =
      editor.revisions();
  EXPECT_GT(segments_revisions_after_live_update.asset_revision,
            segments_revisions_before.asset_revision);
  const quader::QuaderToolSettingsTarget live_segments_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *live_segments_setting =
      find_setting(live_segments_settings,
                   "operation.edge_bevel.segments");
  ASSERT_NE(live_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(live_segments_setting->number_value,
                   static_cast<double>(pending_segments));
  send_mouse_event(*segments_slider, QEvent::MouseButtonRelease,
                   segments_moves.back(), Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(segments_slider->isSliderDown());
  EXPECT_EQ(segments_slider_changes, 3);
  EXPECT_EQ(segments_spin_changes, 0);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_after_live_update.asset_revision);
  const quader::QuaderToolSettingsTarget applied_segments_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *applied_segments_setting =
      find_setting(applied_segments_settings,
                   "operation.edge_bevel.segments");
  ASSERT_NE(applied_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(applied_segments_setting->number_value,
                   static_cast<double>(pending_segments));

  RecordProperty("width_slider_callbacks", width_slider_changes);
  RecordProperty("width_paired_spin_callbacks", width_spin_changes);
  RecordProperty("width_editor_rebuilds", width_slider_destroys);
  RecordProperty("width_pointer_move_us",
                 static_cast<int>(width_elapsed.count()));
  RecordProperty("width_final_flush_us",
                 static_cast<int>(width_release_elapsed.count()));
  RecordProperty("segments_slider_callbacks", segments_slider_changes);
  RecordProperty("segments_paired_spin_callbacks", segments_spin_changes);
  RecordProperty("segments_editor_rebuilds", segments_slider_destroys);
  RecordProperty("segments_three_pointer_moves_us",
                 static_cast<int>(segments_elapsed.count()));

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  window.unbind();
  window.hide();
}
