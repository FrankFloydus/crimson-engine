////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/localization_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_tool_settings_drag_spinner.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_status_message.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/ui_panel_manager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMouseEvent>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QRegion>
#include <QScrollArea>
#include <QScopeGuard>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

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

void send_text_event(QWidget &target, QChar character) {
  QKeyEvent event(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                  QString(character));
  QCoreApplication::sendEvent(&target, &event);
}

void send_wheel_event(QWidget &target, QPoint position, QPoint angle_delta) {
  const QPoint global = target.mapToGlobal(position);
  QWheelEvent event(QPointF(position), QPointF(global), {}, angle_delta,
                    Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(&target, &event);
}

TEST(QtToolSettingsPanelTests,
     AppliesInterfaceScaleToStaticAndDeferredControlMetrics) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsPanel panel;
  auto *axis = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.uv_peel.axis"));
  auto *texture_scale = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_x"));
  ASSERT_NE(axis, nullptr);
  ASSERT_NE(texture_scale, nullptr);
  ASSERT_NE(panel.layout(), nullptr);

  panel.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  // The panel is dock content: its minimum must not force a restored or
  // user-resized dock wider when interface scale changes.
  EXPECT_EQ(panel.minimumWidth(), 240);
  EXPECT_EQ(axis->minimumWidth(), 176);
  EXPECT_EQ(texture_scale->minimumWidth(), 128);
  EXPECT_EQ(panel.layout()->contentsMargins(), QMargins(16, 16, 16, 16));

  panel.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(panel.minimumWidth(), 240);
  EXPECT_EQ(axis->minimumWidth(), 88);
  EXPECT_EQ(texture_scale->minimumWidth(), 64);
  EXPECT_EQ(panel.layout()->contentsMargins(), QMargins(8, 8, 8, 8));
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

void process_events_for(int milliseconds) {
  QEventLoop event_loop;
  QTimer::singleShot(milliseconds, &event_loop, &QEventLoop::quit);
  event_loop.exec();
}

template <typename Predicate>
bool process_events_until(Predicate condition, int timeout_milliseconds) {
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_milliseconds);
  while (!condition() && std::chrono::steady_clock::now() < deadline) {
    process_events_for(25);
  }
  return condition();
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

const quader::QuaderToolSettingNumberComponent *find_vector_component(
    const quader::QuaderToolSettingsTarget &target, std::string_view id) {
  for (const quader::QuaderToolSettingControl &control : target.controls) {
    if (control.kind !=
        quader::QuaderToolSettingControlKind::Vector3Number) {
      continue;
    }
    const auto found = std::find_if(
        control.vector3_components.begin(), control.vector3_components.end(),
        [id](const quader::QuaderToolSettingNumberComponent &component) {
          return component.id == id;
        });
    if (found != control.vector3_components.end()) {
      return &*found;
    }
  }
  return nullptr;
}

quader::QuaderApiVoidResult set_face_texture_field(
    quader::QuaderEditorApi &editor, quader::QuaderFaceTextureField field,
    double value) {
  return editor.modeling().set_face_texture_setting(
      {.field = field,
       .value = value,
       .selection_token =
           editor.modeling().face_texture_settings().selection_token});
}

void type_spin_text(QDoubleSpinBox &spin, const QString &text) {
  spin.setFocus(Qt::OtherFocusReason);
  spin.selectAll();
  for (QChar character : text) {
    QKeyEvent event(QEvent::KeyPress, character.unicode(), Qt::NoModifier,
                    QString(character));
    QCoreApplication::sendEvent(&spin, &event);
  }
}

void finish_spin_edit(QDoubleSpinBox &spin) {
  QMetaObject::invokeMethod(&spin, "editingFinished", Qt::DirectConnection);
}

quader::QuaderApiResult<quader::QuaderModelingObjectRef>
create_texture_action_surface(quader::QuaderEditorApi &editor) {
  quader_poly::Document document;
  constexpr std::array<quader::QVec3, 4> kPositions{{
      {2.0f, 1.0f, 3.0f}, {8.0f, 2.0f, 3.0f},
      {7.0f, 3.0f, 7.0f}, {1.0f, 2.0f, 7.0f},
  }};
  std::array<quader_poly::ElementId, 4> vertices{};
  for (std::size_t index = 0; index < vertices.size(); ++index) {
    vertices[index] = quader_poly::add_vertex(document, kPositions[index]);
  }
  EXPECT_EQ(quader_poly::add_face(document, vertices), 1);
  return editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(
          std::move(document)),
      {.name = "Texture Action Surface"});
}

void expect_same_texture_mesh(const quader::QuaderMeshAsset &actual,
                              const quader::QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &left = actual.vertices[index];
    const auto &right = expected.vertices[index];
    EXPECT_FLOAT_EQ(left.position.x, right.position.x);
    EXPECT_FLOAT_EQ(left.position.y, right.position.y);
    EXPECT_FLOAT_EQ(left.position.z, right.position.z);
    EXPECT_FLOAT_EQ(left.normal.x, right.normal.x);
    EXPECT_FLOAT_EQ(left.normal.y, right.normal.y);
    EXPECT_FLOAT_EQ(left.normal.z, right.normal.z);
    EXPECT_FLOAT_EQ(left.uv.x, right.uv.x);
    EXPECT_FLOAT_EQ(left.uv.y, right.uv.y);
    EXPECT_EQ(left.uv_carrier, right.uv_carrier);
  }
  ASSERT_EQ(actual.face_textures.size(), expected.face_textures.size());
  for (std::size_t index = 0; index < actual.face_textures.size(); ++index) {
    const auto &left = actual.face_textures[index];
    const auto &right = expected.face_textures[index];
    EXPECT_EQ(left.face, right.face);
    EXPECT_FLOAT_EQ(left.scale.x, right.scale.x);
    EXPECT_FLOAT_EQ(left.scale.y, right.scale.y);
    EXPECT_FLOAT_EQ(left.shift_uv.x, right.shift_uv.x);
    EXPECT_FLOAT_EQ(left.shift_uv.y, right.shift_uv.y);
    EXPECT_FLOAT_EQ(left.rotation_degrees, right.rotation_degrees);
  }
}

/// Counts presentation invalidations without depending on Qt paint delivery.
class TexturePresentationEvents final : public QObject {
public:
  int changes = 0;

protected:
  bool eventFilter(QObject *, QEvent *event) override {
    if (event->type() == QEvent::EnabledChange ||
        event->type() == QEvent::ToolTipChange ||
        event->type() == QEvent::LayoutRequest) {
      ++changes;
    }
    return false;
  }
};

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
     StepwiseInteractionUsesNativePointerTrackingAndValueSpaceTicks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-stepwise-slider-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsNumberSlider slider(Qt::Horizontal);
  slider.setRange(2, 128);
  slider.setValue(4);
  slider.setSingleStep(1);
  slider.setPageStep(10);
  slider.set_stepwise_interaction(true);
  slider.setTracking(true);
  slider.setAttribute(Qt::WA_DontShowOnScreen, true);
  slider.resize(240, 32);
  slider.show();
  QCoreApplication::processEvents();

  EXPECT_TRUE(slider.stepwise_interaction());
  EXPECT_TRUE(slider.hasTracking());
  EXPECT_EQ(slider.singleStep(), 1);
  EXPECT_EQ(slider.pageStep(), 1);
  EXPECT_EQ(slider.tickInterval(), 1);
  EXPECT_EQ(slider.tickPosition(), QSlider::TicksBelow);
  const QRect handle = slider_handle(slider);
  ASSERT_FALSE(handle.isEmpty());
  const QPoint press = handle.center();
  const QRect groove = slider_groove(slider);
  ASSERT_FALSE(groove.isEmpty());
  const QPoint drag_position(
      groove.left() + (groove.width() * 3) / 4, groove.center().y());

  send_mouse_event(slider, QEvent::MouseButtonPress, press, Qt::LeftButton,
                   Qt::LeftButton);
  ASSERT_TRUE(slider.isSliderDown());
  send_mouse_event(slider, QEvent::MouseMove, drag_position, Qt::NoButton,
                   Qt::LeftButton);
  const QRect dragged_handle = slider_handle(slider);
  EXPECT_NEAR(dragged_handle.center().x(), drag_position.x(), 1);
  EXPECT_GT(slider.value(), 4);
  EXPECT_EQ(slider.sliderPosition(), slider.value());
  EXPECT_EQ((slider.value() - slider.minimum()) % slider.tickInterval(), 0);
  send_mouse_event(slider, QEvent::MouseButtonRelease, drag_position,
                   Qt::LeftButton,
                   Qt::NoButton);
  EXPECT_FALSE(slider.isSliderDown());

  const int dragged_value = slider.value();
  send_key_event(slider, Qt::Key_Right);
  EXPECT_EQ(slider.value(), dragged_value + 1);
  send_key_event(slider, Qt::Key_PageUp);
  EXPECT_EQ(slider.value(), dragged_value + 2);
  send_key_event(slider, Qt::Key_PageDown);
  EXPECT_EQ(slider.value(), dragged_value + 1);
  send_key_event(slider, Qt::Key_Left);
  EXPECT_EQ(slider.value(), dragged_value);

  send_wheel_event(slider, slider.rect().center(), {0, 120});
  EXPECT_EQ(slider.value(), dragged_value + 1);
  send_wheel_event(slider, slider.rect().center(), {0, -120});
  EXPECT_EQ(slider.value(), dragged_value);

  slider.setValue(4);
  const QPoint stepped_groove_press(groove.right() - 1,
                                    groove.center().y());
  send_mouse_event(slider, QEvent::MouseButtonPress, stepped_groove_press,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(slider, QEvent::MouseButtonRelease, stepped_groove_press,
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(slider.value(), 5)
      << "a native stepped groove press must use pageStep, not absolute scrub";

  slider.set_stepwise_interaction(false);
  EXPECT_FALSE(slider.stepwise_interaction());
  EXPECT_EQ(slider.tickInterval(), 0);
  EXPECT_EQ(slider.tickPosition(), QSlider::NoTicks);
  EXPECT_TRUE(slider.hasTracking());

  slider.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     IdleCenterAdvertisesHorizontalDragAndStationaryClickEditsAllText) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-click-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(2, 128);
  spinner.set_single_step(1);
  spinner.set_value(12);
  spinner.setAttribute(Qt::WA_DontShowOnScreen, true);
  spinner.resize(240, 32);
  spinner.show();
  QCoreApplication::processEvents();

  auto *center = spinner.findChild<QSpinBox *>(
      QStringLiteral("drag_spinner.editor"));
  ASSERT_NE(center, nullptr);
  auto *editor = center->findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(editor->cursor().shape(), Qt::SizeHorCursor);

  const QPoint press = editor->rect().center();
  send_mouse_event(*editor, QEvent::MouseButtonPress, press, Qt::LeftButton,
                   Qt::LeftButton);
  const QPoint sub_threshold(
      press.x() + std::max(0, QApplication::startDragDistance() - 1),
      press.y());
  send_mouse_event(*editor, QEvent::MouseMove, sub_threshold, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_FALSE(spinner.is_dragging());
  EXPECT_EQ(spinner.value(), 12);
  send_mouse_event(*editor, QEvent::MouseButtonRelease, sub_threshold,
                   Qt::LeftButton, Qt::NoButton);
  QCoreApplication::processEvents();

  EXPECT_FALSE(spinner.is_dragging());
  EXPECT_EQ(spinner.value(), 12);
  EXPECT_TRUE(editor->hasSelectedText());
  EXPECT_EQ(editor->selectedText(), editor->text());
  EXPECT_EQ(editor->cursor().shape(), Qt::IBeamCursor);
  EXPECT_TRUE(spinner.is_editing());

  spinner.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     TextEntryCommitsWithEnterAndEscapeRestoresTheCommittedValue) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-edit-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(2, 128);
  spinner.set_value(12);
  spinner.setAttribute(Qt::WA_DontShowOnScreen, true);
  spinner.resize(240, 32);
  spinner.show();
  QCoreApplication::processEvents();

  int finished_edits = 0;
  spinner.editing_finished = [&finished_edits]() { ++finished_edits; };

  auto *editor = spinner.findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(editor, nullptr);
  const QPoint click = editor->rect().center();
  send_mouse_event(*editor, QEvent::MouseButtonPress, click, Qt::LeftButton,
                   Qt::LeftButton);
  send_mouse_event(*editor, QEvent::MouseButtonRelease, click, Qt::LeftButton,
                   Qt::NoButton);
  ASSERT_TRUE(editor->hasSelectedText());
  send_text_event(*editor, u'2');
  send_text_event(*editor, u'7');
  send_key_event(*editor, Qt::Key_Return);
  QCoreApplication::processEvents();
  EXPECT_EQ(spinner.value(), 27);
  EXPECT_EQ(finished_edits, 1);

  send_mouse_event(*editor, QEvent::MouseButtonPress, click, Qt::LeftButton,
                   Qt::LeftButton);
  send_mouse_event(*editor, QEvent::MouseButtonRelease, click, Qt::LeftButton,
                   Qt::NoButton);
  ASSERT_TRUE(editor->hasSelectedText());
  send_text_event(*editor, u'9');
  send_text_event(*editor, u'3');
  send_key_event(*editor, Qt::Key_Escape);
  QCoreApplication::processEvents();
  EXPECT_EQ(spinner.value(), 27);
  EXPECT_EQ(finished_edits, 2);

  spinner.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     DragDiscardsThresholdThenMapsEightLogicalPixelsPerStepAndClampsLive) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-scrub-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(2, 20);
  spinner.set_single_step(1);
  spinner.set_value(10);
  spinner.setAttribute(Qt::WA_DontShowOnScreen, true);
  spinner.resize(240, 32);
  spinner.show();
  QCoreApplication::processEvents();

  int drag_starts = 0;
  int drag_finishes = 0;
  spinner.drag_started = [&drag_starts]() { ++drag_starts; };
  spinner.drag_finished = [&drag_finishes]() { ++drag_finishes; };

  auto *editor = spinner.findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(editor, nullptr);
  const QPoint press = editor->rect().center();
  const int threshold = QApplication::startDragDistance();
  const QPoint crossed(press.x() + threshold + 1, press.y());
  send_mouse_event(*editor, QEvent::MouseButtonPress, press, Qt::LeftButton,
                   Qt::LeftButton);
  send_mouse_event(*editor, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton);
  ASSERT_TRUE(spinner.is_dragging());
  EXPECT_EQ(drag_starts, 1);
  EXPECT_EQ(spinner.value(), 10)
      << "the gesture threshold must not be converted into value travel";

  const QPoint seven_pixels_after_origin(crossed.x() + 7, crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, seven_pixels_after_origin,
                   Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 10);
  const QPoint eight_pixels_after_origin(crossed.x() + 8, crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, eight_pixels_after_origin,
                   Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 11);
  const QPoint twenty_four_pixels_after_origin(crossed.x() + 24,
                                                crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, twenty_four_pixels_after_origin,
                   Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 13)
      << "each eight logical horizontal pixels must advance one singleStep";

  const QPoint far_right(crossed.x() + 10'000, crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, far_right, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_EQ(spinner.value(), spinner.maximum());
  const QPoint far_left(crossed.x() - 10'000, crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, far_left, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_EQ(spinner.value(), spinner.minimum());

  send_mouse_event(*editor, QEvent::MouseButtonRelease, far_left,
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(spinner.is_dragging());
  EXPECT_EQ(drag_finishes, 1);
  EXPECT_GE(spinner.value(), spinner.minimum());
  EXPECT_LE(spinner.value(), spinner.maximum());

  spinner.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     FiveDigitEditorRemainsCompactInsideAWideSettingsRow) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-size-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  QWidget row;
  row.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *layout = new QHBoxLayout(&row);
  auto *spinner = new quader::QtToolSettingsDragSpinner(&row);
  spinner->set_range(0, 99'999);
  spinner->set_value(99'999);
  layout->addWidget(spinner, 0, Qt::AlignLeft);
  layout->addStretch(1);
  row.resize(480, 48);
  row.show();
  QCoreApplication::processEvents();

  auto *center = spinner->spin_box();
  auto *editor = center->findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(editor, nullptr);
  EXPECT_EQ(spinner->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
  EXPECT_EQ(center->sizePolicy().horizontalPolicy(), QSizePolicy::Fixed);
  EXPECT_EQ(spinner->width(), spinner->sizeHint().width());
  EXPECT_LT(spinner->width(), row.width() / 2);
  EXPECT_GE(editor->contentsRect().width(),
            editor->fontMetrics().horizontalAdvance(QStringLiteral("99999")));

  row.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     ArrowButtonsApplyExactlyOneStepAndDisableAtTheirBounds) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-arrow-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(2, 8);
  spinner.set_single_step(2);
  spinner.set_value(6);
  spinner.setAttribute(Qt::WA_DontShowOnScreen, true);
  spinner.resize(240, 32);
  spinner.show();
  QCoreApplication::processEvents();

  QToolButton *left = spinner.findChild<QToolButton *>(
      QStringLiteral("drag_spinner.decrease"));
  QToolButton *right = spinner.findChild<QToolButton *>(
      QStringLiteral("drag_spinner.increase"));
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_TRUE(left->isEnabled());
  EXPECT_TRUE(right->isEnabled());

  left->click();
  EXPECT_EQ(spinner.value(), 4);
  left->click();
  EXPECT_EQ(spinner.value(), 2);
  EXPECT_FALSE(left->isEnabled());
  left->click();
  EXPECT_EQ(spinner.value(), 2);

  right->click();
  EXPECT_EQ(spinner.value(), 4);
  spinner.set_value(8);
  EXPECT_FALSE(right->isEnabled());
  right->click();
  EXPECT_EQ(spinner.value(), 8);

  spinner.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     LiveInterfaceScaleUpdatesArrowHitTargetsWithoutDrift) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::QtUiScaleApplicationController scale_controller(application);

  quader::QtToolSettingsDragSpinner spinner;
  auto *decrease = spinner.findChild<QToolButton *>(
      QStringLiteral("drag_spinner.decrease"));
  auto *increase = spinner.findChild<QToolButton *>(
      QStringLiteral("drag_spinner.increase"));
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);

  const auto expected_extent = [&spinner]() {
    return std::max(
        spinner.style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr,
                                     &spinner),
        spinner.spin_box()->sizeHint().height());
  };
  const int baseline_extent = expected_extent();
  EXPECT_EQ(decrease->minimumWidth(), baseline_extent);
  EXPECT_EQ(decrease->maximumWidth(), baseline_extent);
  EXPECT_EQ(increase->minimumWidth(), baseline_extent);
  EXPECT_EQ(increase->maximumWidth(), baseline_extent);

  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  QCoreApplication::processEvents();
  const int doubled_extent = expected_extent();
  EXPECT_GT(doubled_extent, baseline_extent);
  EXPECT_EQ(decrease->minimumWidth(), doubled_extent);
  EXPECT_EQ(decrease->maximumWidth(), doubled_extent);
  EXPECT_EQ(increase->minimumWidth(), doubled_extent);
  EXPECT_EQ(increase->maximumWidth(), doubled_extent);

  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  QCoreApplication::processEvents();
  EXPECT_EQ(expected_extent(), baseline_extent);
  EXPECT_EQ(decrease->minimumWidth(), baseline_extent);
  EXPECT_EQ(decrease->maximumWidth(), baseline_extent);
  EXPECT_EQ(increase->minimumWidth(), baseline_extent);
  EXPECT_EQ(increase->maximumWidth(), baseline_extent);
}

TEST(QtToolSettingsDragSpinnerTests,
     PathArrayPanelRetainsSpinnerQueuesArrowsAndFlushesReleasedDrag) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-path-array-drag-spinner-panel-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {32.0F, 0.0F, 0.0F}}},
      .name = "Path Array Spinner Driver",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::PathArray).ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *spinner = dynamic_cast<quader::QtToolSettingsDragSpinner *>(
      panel.findChild<QWidget *>(QStringLiteral(
          "tool_settings.control.path_array.count.spinner")));
  ASSERT_NE(spinner, nullptr);
  QPointer<quader::QtToolSettingsDragSpinner> retained_spinner(spinner);
  auto *center = panel.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.path_array.count"));
  auto *line_edit = spinner->findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  auto *decrease = spinner->findChild<QToolButton *>(
      QStringLiteral("drag_spinner.decrease"));
  auto *increase = spinner->findChild<QToolButton *>(
      QStringLiteral("drag_spinner.increase"));
  ASSERT_NE(center, nullptr);
  ASSERT_NE(line_edit, nullptr);
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);
  EXPECT_EQ(spinner->spin_box(), center);
  EXPECT_EQ(panel.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.path_array.count.slider")),
            nullptr);
  EXPECT_NE(panel.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.path_array.distance.slider")),
            nullptr);

  const auto owner_count = [&editor]() {
    const quader::QuaderToolSettingsTarget target =
        editor.tools().active_settings();
    const quader::QuaderToolSettingControl *control =
        find_setting(target, "path_array.count");
    return control == nullptr
               ? -1
               : static_cast<int>(std::llround(control->number_value));
  };

  ASSERT_EQ(owner_count(), 4);
  increase->click();
  EXPECT_EQ(spinner->value(), 5);
  EXPECT_EQ(owner_count(), 4);
  ASSERT_TRUE(process_events_until([&] { return owner_count() == 5; }, 1000));
  EXPECT_EQ(owner_count(), 5);
  decrease->click();
  EXPECT_EQ(spinner->value(), 4);
  ASSERT_TRUE(process_events_until([&] { return owner_count() == 4; }, 1000));
  EXPECT_EQ(owner_count(), 4);

  const QPoint press = line_edit->rect().center();
  const QPoint crossed(press.x() + QApplication::startDragDistance() + 1,
                       press.y());
  const QPoint three_steps(crossed.x() + 24, crossed.y());
  send_mouse_event(*line_edit, QEvent::MouseButtonPress, press,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseMove, three_steps, Qt::NoButton,
                   Qt::LeftButton);
  ASSERT_TRUE(spinner->is_dragging());
  EXPECT_TRUE(panel.numeric_interaction_active());
  EXPECT_EQ(spinner->value(), 7);
  EXPECT_EQ(owner_count(), 4);
  send_mouse_event(*line_edit, QEvent::MouseButtonRelease, three_steps,
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(spinner->is_dragging());
  EXPECT_EQ(owner_count(), 7)
      << "drag release must synchronously flush the coalesced owner update";

  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.count", 8.0)
                  .ok());
  panel.refresh();
  EXPECT_EQ(dynamic_cast<quader::QtToolSettingsDragSpinner *>(
                panel.findChild<QWidget *>(QStringLiteral(
                    "tool_settings.control.path_array.count.spinner"))),
            retained_spinner.data());
  EXPECT_EQ(spinner->value(), 8);

  panel.unbind();
  panel.hide();
}

TEST(QtToolSettingsDragSpinnerTests,
     PathArrayPanelDefersStaleRefreshDuringDragAndTextEditing) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-path-array-spinner-refresh-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {32.0F, 0.0F, 0.0F}}},
      .name = "Path Array Spinner Refresh Driver",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::PathArray).ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *spinner = dynamic_cast<quader::QtToolSettingsDragSpinner *>(
      panel.findChild<QWidget *>(QStringLiteral(
          "tool_settings.control.path_array.count.spinner")));
  ASSERT_NE(spinner, nullptr);
  auto *line_edit = spinner->findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(line_edit, nullptr);

  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.count", 10.0)
                  .ok());
  panel.refresh();
  ASSERT_EQ(spinner->value(), 10);
  const QPoint press = line_edit->rect().center();
  const QPoint crossed(press.x() + QApplication::startDragDistance() + 1,
                       press.y());
  const QPoint one_step(crossed.x() + 8, crossed.y());
  send_mouse_event(*line_edit, QEvent::MouseButtonPress, press,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseMove, one_step, Qt::NoButton,
                   Qt::LeftButton);
  ASSERT_TRUE(spinner->is_dragging());
  ASSERT_EQ(spinner->value(), 11);
  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.count", 40.0)
                  .ok());
  panel.refresh();
  EXPECT_EQ(spinner->value(), 11)
      << "an owner refresh must not move an actively dragged local value";
  send_mouse_event(*line_edit, QEvent::MouseButtonRelease, one_step,
                   Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(process_events_until([&] { return spinner->value() == 40; }, 1000));
  EXPECT_EQ(spinner->value(), 40)
      << "the invalidated local dispatch must reconcile after release";

  const QPoint click = line_edit->rect().center();
  send_mouse_event(*line_edit, QEvent::MouseButtonPress, click,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseButtonRelease, click,
                   Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(spinner->is_editing());
  ASSERT_TRUE(line_edit->hasSelectedText());
  send_text_event(*line_edit, u'9');
  send_text_event(*line_edit, u'9');
  ASSERT_EQ(line_edit->text(), QStringLiteral("99"));
  ASSERT_EQ(spinner->value(), 40)
      << "uncommitted text must not change the spin box value";
  process_events_for(125);
  const quader::QuaderToolSettingsTarget editing_target =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *editing_count =
      find_setting(editing_target, "path_array.count");
  ASSERT_NE(editing_count, nullptr);
  EXPECT_EQ(static_cast<int>(std::llround(editing_count->number_value)), 40)
      << "text entry must remain local until Enter or focus-out commits it";
  EXPECT_EQ(line_edit->text(), QStringLiteral("99"));
  ASSERT_TRUE(editor.tools()
                  .set_setting("path_array", "path_array.count", 30.0)
                  .ok());
  panel.refresh();
  EXPECT_EQ(line_edit->text(), QStringLiteral("99"))
      << "an owner refresh must not replace active text input";
  send_key_event(*line_edit, Qt::Key_Escape);
  ASSERT_TRUE(process_events_until(
      [&] { return !spinner->is_editing() && spinner->value() == 30; }, 1000));
  EXPECT_FALSE(spinner->is_editing());
  EXPECT_EQ(spinner->value(), 30)
      << "cancelled stale text input must reconcile to current owner state";

  panel.unbind();
  panel.hide();
}

TEST(QtToolSettingsPanelTests,
     DescriptorGroupsRetainDisclosureAndVector3ComponentsDispatchById) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-groups-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QuaderStandaloneEditor standalone =
      quader::create_standalone_editor();
  quader::QuaderEditorApi &editor = standalone.editor();
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {32.0F, 0.0F, 0.0F}}},
      .name = "Path Array Group Driver",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::PathArray).ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *placement_header = panel.findChild<QToolButton *>(QStringLiteral(
      "tool_settings.group.path_array.group.placement.header"));
  auto *rotation_header = panel.findChild<QToolButton *>(QStringLiteral(
      "tool_settings.group.path_array.group.rotation.header"));
  auto *rotation_content = panel.findChild<QWidget *>(QStringLiteral(
      "tool_settings.group.path_array.group.rotation.content"));
  auto *scale_header = panel.findChild<QToolButton *>(QStringLiteral(
      "tool_settings.group.path_array.group.scale.header"));
  ASSERT_NE(placement_header, nullptr);
  ASSERT_NE(rotation_header, nullptr);
  ASSERT_NE(rotation_content, nullptr);
  ASSERT_NE(scale_header, nullptr);
  EXPECT_TRUE(placement_header->isChecked());
  EXPECT_FALSE(rotation_header->isChecked());
  EXPECT_TRUE(rotation_content->isHidden());
  EXPECT_FALSE(scale_header->isChecked());

  auto *rotation_x = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.path_array.rotation_offset.x"));
  auto *rotation_y = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.path_array.rotation_offset.y"));
  auto *rotation_z = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.path_array.rotation_offset.z"));
  ASSERT_NE(rotation_x, nullptr);
  ASSERT_NE(rotation_y, nullptr);
  ASSERT_NE(rotation_z, nullptr);
  EXPECT_EQ(panel.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.path_array.rotation_offset.x.slider")),
            nullptr);
  EXPECT_NE(dynamic_cast<quader::QtToolSettingsDragSpinner *>(
                panel.findChild<QWidget *>(QStringLiteral(
                    "tool_settings.control.path_array.count.spinner"))),
            nullptr)
      << "ungrouped control presentation must remain available in groups";

  rotation_header->click();
  ASSERT_TRUE(rotation_header->isChecked());
  ASSERT_FALSE(rotation_content->isHidden());
  QPointer<QDoubleSpinBox> retained_rotation_x(rotation_x);
  rotation_x->setValue(12.5);
  EXPECT_TRUE(panel.numeric_interaction_active());
  ASSERT_TRUE(process_events_until([&] {
    const auto target = editor.tools().active_settings();
    const auto *component =
        find_vector_component(target, "path_array.rotation_offset.x");
    return component != nullptr && component->value == 12.5;
  }, 1000));
  const auto rotation_target = editor.tools().active_settings();
  const quader::QuaderToolSettingNumberComponent *owner_rotation_x =
      find_vector_component(rotation_target,
                            "path_array.rotation_offset.x");
  ASSERT_NE(owner_rotation_x, nullptr);
  EXPECT_DOUBLE_EQ(owner_rotation_x->value, 12.5);

  panel.refresh();
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.path_array.rotation_offset.x")),
            retained_rotation_x.data());
  EXPECT_TRUE(rotation_header->isChecked());

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "it-IT"));
  panel.set_localization(italian);
  QCoreApplication::processEvents();
  rotation_header = panel.findChild<QToolButton *>(QStringLiteral(
      "tool_settings.group.path_array.group.rotation.header"));
  ASSERT_NE(rotation_header, nullptr);
  EXPECT_TRUE(rotation_header->isChecked())
      << "a descriptor rebuild must restore disclosure state by stable id";
  EXPECT_EQ(rotation_header->text(), QStringLiteral("Rotazione"));

  panel.unbind();
  panel.hide();
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

  quader::QtToolSettingsPanel window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.bind(editor);
  window.show();
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

  const int local_copies_during_drag = copies->value();
  ASSERT_NE(local_copies_during_drag, 16);
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
  EXPECT_EQ(retained_copies->value(), local_copies_during_drag);
  EXPECT_EQ(retained_repeats->maximum(), 64);
  retained_copies->setSliderDown(false);
  window.refresh();
  EXPECT_EQ(retained_copies->value(), 16);

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

TEST(QtToolSettingsPanelTests,
     CollapsibleSectionsDescribeAndResetTheActiveToolWithoutClosingThePanel) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-panel-tests";
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
          "Tool Settings Panel Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::RadialArray).ok());

  const quader::QuaderToolSettingsTarget defaults =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *default_copies =
      find_setting(defaults, "radial_array.copies");
  ASSERT_NE(default_copies, nullptr);
  ASSERT_TRUE(editor.tools()
                  .set_setting("radial_array", "radial_array.copies",
                               default_copies->number_value + 3.0)
                  .ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *description_header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.description.header"));
  auto *description_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.description"));
  auto *description_content = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.description.content"));
  auto *settings_header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.settings.header"));
  auto *settings_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.settings"));
  auto *settings_content = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.settings.content"));
  auto *description = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.description"));
  auto *reset = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.reset_defaults"));
  auto *copies_label = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.control.radial_array.copies.label"));
  auto *copies_slider =
      dynamic_cast<quader::QtToolSettingsNumberSlider *>(
          panel.findChild<QSlider *>(QStringLiteral(
              "tool_settings.control.radial_array.copies.slider")));
  ASSERT_NE(description_header, nullptr);
  ASSERT_NE(description_section, nullptr);
  ASSERT_NE(description_content, nullptr);
  ASSERT_NE(settings_header, nullptr);
  ASSERT_NE(settings_section, nullptr);
  ASSERT_NE(settings_content, nullptr);
  ASSERT_NE(description, nullptr);
  ASSERT_NE(reset, nullptr);
  ASSERT_NE(copies_label, nullptr);
  ASSERT_NE(copies_slider, nullptr);
  EXPECT_TRUE(panel.has_active_target());
  EXPECT_FALSE(description->text().isEmpty());
  EXPECT_TRUE(description_header->isChecked());
  EXPECT_TRUE(settings_header->isChecked());
  EXPECT_FALSE(description_section->isHidden());
  EXPECT_FALSE(settings_section->isHidden());
  EXPECT_FALSE(description_content->isHidden());
  EXPECT_FALSE(settings_content->isHidden());
  EXPECT_TRUE(reset->isEnabled());
  EXPECT_LE(copies_label->geometry().bottom(),
            copies_slider->geometry().top());

  settings_header->click();
  EXPECT_FALSE(settings_header->isChecked());
  EXPECT_TRUE(settings_content->isHidden());
  panel.refresh();
  EXPECT_TRUE(settings_content->isHidden());
  settings_header->click();
  EXPECT_FALSE(settings_content->isHidden());

  reset->click();
  QCoreApplication::processEvents();
  const auto reset_target = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *reset_copies =
      find_setting(reset_target, "radial_array.copies");
  ASSERT_NE(reset_copies, nullptr);
  EXPECT_DOUBLE_EQ(reset_copies->number_value, default_copies->number_value);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  panel.refresh();
  EXPECT_FALSE(panel.has_active_target());
  EXPECT_FALSE(panel.isHidden());
  EXPECT_FALSE(reset->isEnabled());
  EXPECT_TRUE(description_section->isHidden());
  EXPECT_TRUE(settings_section->isHidden());
  auto *target_title = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.target_title"));
  ASSERT_NE(target_title, nullptr);
  EXPECT_EQ(target_title->text(), QStringLiteral("No Active Tool"));
  EXPECT_FALSE(target_title->isHidden());

  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Cut).ok());
  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "it-IT"));
  panel.set_localization(italian);
  QCoreApplication::processEvents();
  auto *discard_left = panel.findChild<QRadioButton *>(
      QStringLiteral(
          "tool_settings.control.operation.cut.keep_mode.option.discard_left"));
  ASSERT_NE(discard_left, nullptr);
  EXPECT_FALSE(description_section->isHidden());
  EXPECT_FALSE(settings_section->isHidden());
  EXPECT_EQ(discard_left->text(), QStringLiteral("Scarta sinistra"));
  EXPECT_EQ(target_title->text(), QStringLiteral("Taglia"));
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_EQ(reset->text(), QStringLiteral("Ripristina predefiniti"));

  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::LinearArray).ok());
  panel.refresh();
  auto *linear_count_label = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.control.linear_array.count.label"));
  ASSERT_NE(linear_count_label, nullptr);
  EXPECT_EQ(linear_count_label->text(), QStringLiteral("Numero di copie"));
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     FaceTextureSectionRetainsEditableMixedScalesAndSingleFacePixelShifts) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  // Match main.cpp's numeric editing contract without changing later tests.
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-face-texture-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Texture Settings Faces");
  ASSERT_TRUE(source.ok()) << source.error_message();

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(330, 650);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.texture"));
  auto *description_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.description"));
  auto *settings_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.settings"));
  auto *header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.texture.header"));
  auto *content = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.texture.content"));
  auto *scale_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_x"));
  auto *scale_y = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_y"));
  auto *shift_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.shift_x"));
  auto *shift_y = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.shift_y"));
  auto *rotate = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.rotate"));
  auto *target_title = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.target_title"));
  auto *title_separator = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.title_separator"));
  auto *edit_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.edit_texture"));
  auto *edit_header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.edit_texture.header"));
  auto *edit_content = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.edit_texture.content"));
  auto *treat_as_one = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.edit_texture.treat_as_one"));
  auto *shift_left = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.edit_texture.shift_left"));
  ASSERT_NE(section, nullptr);
  ASSERT_NE(description_section, nullptr);
  ASSERT_NE(settings_section, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(scale_x, nullptr);
  ASSERT_NE(scale_y, nullptr);
  ASSERT_NE(shift_x, nullptr);
  ASSERT_NE(shift_y, nullptr);
  ASSERT_NE(rotate, nullptr);
  ASSERT_NE(target_title, nullptr);
  ASSERT_NE(title_separator, nullptr);
  ASSERT_NE(edit_section, nullptr);
  ASSERT_NE(edit_header, nullptr);
  ASSERT_NE(edit_content, nullptr);
  ASSERT_NE(treat_as_one, nullptr);
  ASSERT_NE(shift_left, nullptr);
  EXPECT_EQ(shift_x->accessibleName(), QStringLiteral("Shift X"));
  EXPECT_EQ(shift_y->accessibleName(), QStringLiteral("Shift Y"));
  ASSERT_EQ(scale_y->locale().decimalPoint(), QStringLiteral("."));
  EXPECT_TRUE(section->isHidden());
  EXPECT_TRUE(edit_section->isHidden());
  EXPECT_TRUE(description_section->isHidden());
  EXPECT_TRUE(settings_section->isHidden());
  EXPECT_TRUE(edit_header->isChecked());
  EXPECT_FALSE(treat_as_one->isChecked());
  EXPECT_EQ(edit_section->findChildren<QPushButton *>().size(), 25);
  EXPECT_TRUE(header->isChecked());
  EXPECT_FALSE(panel.has_active_target());
  EXPECT_TRUE(section->findChildren<QSlider *>().isEmpty());
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_FALSE(title_separator->isHidden());
  const QPointer<QDoubleSpinBox> retained_scale_y(scale_y);

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleY, 2.0).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleY, 4.0).ok());
  ASSERT_TRUE(editor.selection().select_faces(source.value(), {1, 2}).ok());
  panel.refresh_texture_settings();
  QCoreApplication::processEvents();
  EXPECT_TRUE(target_title->isHidden());
  EXPECT_TRUE(title_separator->isHidden());
  EXPECT_EQ(scale_x->geometry().top(), scale_y->geometry().top());
  EXPECT_LT(scale_x->geometry().right(), scale_y->geometry().left());
  EXPECT_EQ(shift_x->geometry().top(), shift_y->geometry().top());
  EXPECT_LT(shift_x->geometry().right(), shift_y->geometry().left());
  EXPECT_LT(scale_x->geometry().bottom(), shift_x->geometry().top());
  EXPECT_LT(shift_x->geometry().bottom(), rotate->geometry().top());
  EXPECT_EQ(scale_x->geometry().left(), shift_x->geometry().left());
  EXPECT_EQ(scale_y->geometry().left(), shift_y->geometry().left());
  EXPECT_EQ(rotate->geometry().left(), scale_x->geometry().left());
  EXPECT_EQ(rotate->geometry().right(), scale_y->geometry().right());
  for (QDoubleSpinBox *spin : {scale_x, scale_y, shift_x, shift_y, rotate}) {
    EXPECT_TRUE(content->rect().contains(spin->geometry()));
  }
  TexturePresentationEvents heading_events;
  target_title->installEventFilter(&heading_events);
  title_separator->installEventFilter(&heading_events);
  for (int frame = 0; frame < 20; ++frame) {
    panel.refresh_texture_settings();
  }
  EXPECT_EQ(heading_events.changes, 0);
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  EXPECT_TRUE(description_section->isHidden());
  EXPECT_TRUE(settings_section->isHidden());
  EXPECT_FALSE(panel.has_active_target());
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);
  EXPECT_EQ(scale_y->text(), QStringLiteral("--"));
  EXPECT_TRUE(scale_y->isEnabled());
  EXPECT_EQ(shift_x->text(), QStringLiteral("--"));
  EXPECT_EQ(shift_y->text(), QStringLiteral("--"));
  EXPECT_FALSE(shift_x->isEnabled());
  EXPECT_FALSE(shift_y->isEnabled());
  EXPECT_TRUE(shift_left->isEnabled());
  const auto before_group_toggle = editor.revisions();
  const auto history_before_group_toggle = editor.history().snapshot();
  treat_as_one->click();
  EXPECT_TRUE(editor.modeling().face_texture_treat_as_one());
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            before_group_toggle.modeling_content_revision);
  EXPECT_EQ(editor.revisions().scene_revision,
            before_group_toggle.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision,
            before_group_toggle.asset_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label,
            history_before_group_toggle.undo_label);
  edit_header->click();
  EXPECT_TRUE(edit_content->isHidden());
  EXPECT_FALSE(content->isHidden());

  // Exercise native interpretation and focus traversal, not only the signal:
  // accepting an untouched mixed field must preserve both original values.
  panel.activateWindow();
  scale_y->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_TRUE(scale_y->hasFocus() ||
              (QApplication::focusWidget() != nullptr &&
               scale_y->isAncestorOf(QApplication::focusWidget())));
  const auto untouched_revision = editor.revisions();
  send_key_event(*scale_y, Qt::Key_Return);
  EXPECT_EQ(scale_y->text(), QStringLiteral("--"));
  EXPECT_FALSE(editor.modeling().face_texture_settings().scale_y.has_value());
  send_key_event(*scale_y, Qt::Key_Tab);
  QCoreApplication::processEvents();
  EXPECT_FALSE(scale_y->hasFocus());
  ASSERT_NE(QApplication::focusWidget(), nullptr);
  EXPECT_FALSE(scale_y->isAncestorOf(QApplication::focusWidget()));
  process_events_for(75);
  EXPECT_EQ(scale_y->text(), QStringLiteral("--"));
  EXPECT_FALSE(editor.modeling().face_texture_settings().scale_y.has_value());
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            untouched_revision.modeling_content_revision);
  EXPECT_EQ(editor.revisions().scene_revision, untouched_revision.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, untouched_revision.asset_revision);
  EXPECT_FALSE(panel.numeric_interaction_active());

  scale_x->setValue(5.0);
  finish_spin_edit(*scale_x);
  auto settings = editor.modeling().face_texture_settings();
  EXPECT_EQ(settings.scale_x, std::optional<double>(5.0));
  EXPECT_FALSE(settings.scale_y.has_value());

  type_spin_text(*scale_y, QStringLiteral("0.5"));
  panel.refresh();
  EXPECT_EQ(scale_y->text(), QStringLiteral("0.5"));
  ASSERT_TRUE(process_events_until([&] {
    return editor.modeling().face_texture_settings().scale_y ==
           std::optional<double>(0.5);
  }, 1000));
  settings = editor.modeling().face_texture_settings();
  EXPECT_EQ(settings.scale_x, std::optional<double>(5.0));
  EXPECT_EQ(settings.scale_y, std::optional<double>(0.5));
  EXPECT_EQ(scale_y->text(), QStringLiteral("0.5"));
  const auto live_revision = editor.revisions().modeling_content_revision;
  send_key_event(*scale_y, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(scale_y->value(), 0.5);
  EXPECT_EQ(editor.revisions().modeling_content_revision, live_revision);
  EXPECT_FALSE(panel.numeric_interaction_active());

  // Repeat from a mixed value, this time tabbing away before the trailing timer
  // fires. Focus-out must flush the exact typed value once and cancel the timer.
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh_texture_settings();
  EXPECT_EQ(scale_y->text(), QStringLiteral("--"));
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(5.0));
  type_spin_text(*scale_y, QStringLiteral("0.5"));
  panel.refresh();
  EXPECT_EQ(scale_y->text(), QStringLiteral("0.5"));
  EXPECT_FALSE(editor.modeling().face_texture_settings().scale_y.has_value());
  send_key_event(*scale_y, Qt::Key_Tab);
  QCoreApplication::processEvents();
  EXPECT_FALSE(scale_y->hasFocus());
  settings = editor.modeling().face_texture_settings();
  EXPECT_EQ(settings.scale_x, std::optional<double>(5.0));
  EXPECT_EQ(settings.scale_y, std::optional<double>(0.5));
  EXPECT_DOUBLE_EQ(scale_y->value(), 0.5);
  const auto tab_revision = editor.revisions().modeling_content_revision;
  process_events_for(75);
  EXPECT_EQ(editor.revisions().modeling_content_revision, tab_revision);
  EXPECT_FALSE(panel.numeric_interaction_active());

  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  panel.refresh_texture_settings();
  EXPECT_TRUE(shift_x->isEnabled());
  EXPECT_TRUE(shift_y->isEnabled());
  EXPECT_DOUBLE_EQ(scale_x->value(), 5.0);
  EXPECT_DOUBLE_EQ(scale_y->value(), 0.5);
  shift_x->setValue(32.0);
  finish_spin_edit(*shift_x);
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().shift_x, 32.0);
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  panel.refresh_texture_settings();
  EXPECT_DOUBLE_EQ(shift_x->value(), 0.0);
  EXPECT_DOUBLE_EQ(scale_x->value(), 5.0);
  EXPECT_DOUBLE_EQ(scale_y->value(), 0.5);

  header->click();
  EXPECT_TRUE(content->isHidden());
  ASSERT_TRUE(editor.selection().clear().ok());
  panel.refresh_texture_settings();
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_FALSE(title_separator->isHidden());
  EXPECT_TRUE(section->isHidden());
  EXPECT_TRUE(edit_section->isHidden());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  panel.refresh_texture_settings();
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  EXPECT_TRUE(edit_content->isHidden());
  EXPECT_TRUE(treat_as_one->isChecked());
  EXPECT_TRUE(content->isHidden());
  EXPECT_EQ(retained_scale_y.data(), scale_y);
  EXPECT_FALSE(header->isChecked());
  EXPECT_TRUE(target_title->isHidden());
  EXPECT_TRUE(title_separator->isHidden());

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "it-IT"));
  panel.set_localization(italian);
  EXPECT_EQ(header->text(), QStringLiteral("Impostazioni texture"));
  EXPECT_EQ(edit_header->text(), QStringLiteral("Modifica texture"));
  EXPECT_EQ(treat_as_one->text(), QStringLiteral("Tratta come unico"));
  EXPECT_FALSE(shift_left->accessibleName().isEmpty());
  EXPECT_TRUE(shift_left->toolTip().startsWith(shift_left->accessibleName()));
  EXPECT_EQ(scale_x->accessibleName(), QStringLiteral("Scala X"));
  EXPECT_EQ(shift_x->accessibleName(), QStringLiteral("Spostamento X"));
  EXPECT_EQ(shift_y->accessibleName(), QStringLiteral("Spostamento Y"));
  EXPECT_EQ(scale_y->locale().decimalPoint(), QStringLiteral("."));
  EXPECT_TRUE(content->isHidden());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  panel.refresh_texture_settings();
  EXPECT_TRUE(section->isHidden());
  EXPECT_TRUE(edit_section->isHidden());
  panel.unbind();
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_FALSE(title_separator->isHidden());
}

TEST(QtToolSettingsPanelTests,
     EveryEditTextureButtonMatchesItsPublicApiAction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-edit-texture-bindings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  using Action = quader::QuaderFaceTextureAction;
  constexpr std::array<std::pair<const char *, Action>, 25> kCases{{
      {"align_to_grid", Action::AlignToGrid},
      {"align_to_face", Action::AlignToFace},
      {"align_to_view", Action::AlignToView},
      {"scale_x_up", Action::ScaleXUp},
      {"scale_x_down", Action::ScaleXDown},
      {"scale_y_up", Action::ScaleYUp},
      {"scale_y_down", Action::ScaleYDown},
      {"shift_left", Action::ShiftLeft},
      {"shift_right", Action::ShiftRight},
      {"shift_up", Action::ShiftUp},
      {"shift_down", Action::ShiftDown},
      {"rotate_clockwise", Action::RotateClockwise},
      {"rotate_counter_clockwise", Action::RotateCounterClockwise},
      {"flip_texture_horizontal", Action::FlipTextureHorizontal},
      {"flip_texture_vertical", Action::FlipTextureVertical},
      {"flip_face_horizontal", Action::FlipFaceHorizontal},
      {"flip_face_vertical", Action::FlipFaceVertical},
      {"fit_both", Action::FitBoth},
      {"fit_x", Action::FitX},
      {"fit_y", Action::FitY},
      {"justify_left", Action::JustifyLeft},
      {"justify_right", Action::JustifyRight},
      {"justify_top", Action::JustifyTop},
      {"justify_bottom", Action::JustifyBottom},
      {"justify_center", Action::JustifyCenter},
  }};
  for (const auto &[id, action] : kCases) {
    SCOPED_TRACE(id);
    auto standalone = quader::create_standalone_editor();
    auto reference = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    auto &expected_editor = reference.editor();
    std::array<quader::QuaderModelingObjectRef, 2> objects;
    std::size_t object_index = 0;
    for (auto *api : {&editor, &expected_editor}) {
      const auto created = create_texture_action_surface(*api);
      ASSERT_TRUE(created.ok()) << created.error_message();
      objects[object_index++] = created.value();
      ASSERT_TRUE(api->selection().set_mode(quader::QuaderSelectionMode::Face).ok());
      ASSERT_TRUE(api->selection().select_face(created.value(), 1).ok());
      ASSERT_TRUE(set_face_texture_field(
                      *api, quader::QuaderFaceTextureField::ScaleX, 1.7).ok());
      ASSERT_TRUE(set_face_texture_field(
                      *api, quader::QuaderFaceTextureField::ScaleY, -2.3).ok());
      ASSERT_TRUE(set_face_texture_field(
                      *api, quader::QuaderFaceTextureField::ShiftX, 13.0).ok());
      ASSERT_TRUE(set_face_texture_field(
                      *api, quader::QuaderFaceTextureField::ShiftY, -9.0).ok());
      ASSERT_TRUE(set_face_texture_field(
                      *api, quader::QuaderFaceTextureField::Rotate, 23.0).ok());
      api->preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
          .rotation_snap_enabled = [] { return false; },
          .rotation_snap_degrees = [] { return 22.5; },
      });
      ASSERT_TRUE(api->main_viewport().set_view(quader::ViewportView::Top).ok());
    }
    const auto before = editor.modeling().mesh(objects[0]).compiled_mesh();
    ASSERT_TRUE(before.has_value());
    quader::QtToolSettingsPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    auto *button = panel.findChild<QPushButton *>(
        QStringLiteral("tool_settings.edit_texture.%1").arg(
            QString::fromLatin1(id)));
    ASSERT_NE(button, nullptr);
    ASSERT_TRUE(button->isEnabled());
    EXPECT_FALSE(button->accessibleName().isEmpty());
    EXPECT_TRUE(button->toolTip().startsWith(button->accessibleName()));
    const auto expected_result = expected_editor.modeling().apply_face_texture_action(
        {.action = action,
         .selection_token = expected_editor.modeling()
                                .face_texture_settings().selection_token});
    ASSERT_TRUE(expected_result.ok()) << expected_result.error_message();
    button->click();
    const auto actual = editor.modeling().mesh(objects[0]).compiled_mesh();
    const auto expected =
        expected_editor.modeling().mesh(objects[1]).compiled_mesh();
    ASSERT_TRUE(actual.has_value());
    ASSERT_TRUE(expected.has_value());
    expect_same_texture_mesh(*actual, *expected);
    EXPECT_EQ(editor.history().snapshot().undo_label,
              expected_editor.history().snapshot().undo_label);
    if (expected_result.changed()) {
      ASSERT_TRUE(editor.history().undo().ok());
      const auto restored = editor.modeling().mesh(objects[0]).compiled_mesh();
      ASSERT_TRUE(restored.has_value());
      expect_same_texture_mesh(*restored, *before);
    }
    panel.unbind();
  }
}

TEST(QtToolSettingsPanelTests,
     TextureScaleButtonsAndSpinnersTraverseExactBinaryStepsAndRetainHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-texture-binary-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  constexpr std::array<const char *, 24> kExpectedText{{
      "0.0001", "0.0002", "0.0005", "0.0010", "0.0020", "0.0039",
      "0.0078", "0.0156", "0.0313", "0.0625", "0.1250", "0.2500",
      "0.5000", "1.0000", "2.0000", "4.0000", "8.0000", "16.0000",
      "32.0000", "64.0000", "128.0000", "256.0000", "512.0000",
      "1024.0000",
  }};
  using Field = quader::QuaderFaceTextureField;
  const std::array<std::pair<const char *, Field>, 2> axes{{
      {"x", Field::ScaleX}, {"y", Field::ScaleY},
  }};
  for (const bool use_buttons : {true, false}) {
    SCOPED_TRACE(use_buttons ? "edit texture buttons" : "spin arrows");
    for (const auto &[axis, field] : axes) {
      SCOPED_TRACE(axis);
      auto standalone = quader::create_standalone_editor();
      auto &editor = standalone.editor();
      const auto created = create_texture_action_surface(editor);
      ASSERT_TRUE(created.ok()) << created.error_message();
      ASSERT_TRUE(editor.selection()
                      .set_mode(quader::QuaderSelectionMode::Face).ok());
      ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
      ASSERT_TRUE(set_face_texture_field(editor, field, 1.0 / 8192.0).ok());
      quader::QtToolSettingsPanel panel;
      panel.setAttribute(Qt::WA_DontShowOnScreen, true);
      panel.bind(editor);
      panel.show();
      auto *spin = panel.findChild<QDoubleSpinBox *>(
          QStringLiteral("tool_settings.texture.scale_%1")
              .arg(QString::fromLatin1(axis)));
      auto *up = panel.findChild<QPushButton *>(
          QStringLiteral("tool_settings.edit_texture.scale_%1_up")
              .arg(QString::fromLatin1(axis)));
      auto *down = panel.findChild<QPushButton *>(
          QStringLiteral("tool_settings.edit_texture.scale_%1_down")
              .arg(QString::fromLatin1(axis)));
      auto *message = panel.findChild<QLabel *>(
          QStringLiteral("tool_settings.edit_texture.message"));
      ASSERT_NE(spin, nullptr);
      ASSERT_NE(up, nullptr);
      ASSERT_NE(down, nullptr);
      ASSERT_NE(message, nullptr);
      const auto expect_scale = [&](int exponent) {
        const double expected = std::ldexp(1.0, exponent);
        const auto settings = editor.modeling().face_texture_settings();
        EXPECT_EQ(field == Field::ScaleX ? settings.scale_x : settings.scale_y,
                  std::optional<double>(expected));
        EXPECT_EQ(field == Field::ScaleX ? settings.scale_y : settings.scale_x,
                  std::optional<double>(1.0));
        EXPECT_DOUBLE_EQ(spin->value(), expected);
        EXPECT_EQ(spin->text(), QString::fromLatin1(kExpectedText[exponent + 13]));
        EXPECT_TRUE(message->isHidden());
      };
      const auto step = [&](bool upward) {
        if (use_buttons) {
          (upward ? up : down)->click();
        } else {
          if (upward) {
            spin->stepUp();
          } else {
            spin->stepDown();
          }
          finish_spin_edit(*spin);
        }
      };
      const auto expect_endpoint_no_op = [&](bool upward, int exponent) {
        const auto revisions = editor.revisions();
        const auto history = editor.history().snapshot();
        step(upward);
        step(upward);
        expect_scale(exponent);
        const auto after = editor.revisions();
        EXPECT_EQ(after.project_revision, revisions.project_revision);
        EXPECT_EQ(after.scene_revision, revisions.scene_revision);
        EXPECT_EQ(after.scene_layer_revision, revisions.scene_layer_revision);
        EXPECT_EQ(after.asset_revision, revisions.asset_revision);
        EXPECT_EQ(after.modeling_content_revision,
                  revisions.modeling_content_revision);
        EXPECT_EQ(after.modeling_selection_revision,
                  revisions.modeling_selection_revision);
        const auto after_history = editor.history().snapshot();
        EXPECT_EQ(after_history.can_undo, history.can_undo);
        EXPECT_EQ(after_history.can_redo, history.can_redo);
        EXPECT_EQ(after_history.undo_label, history.undo_label);
        EXPECT_EQ(after_history.redo_label, history.redo_label);
      };

      expect_scale(-13);
      for (int exponent = -12; exponent <= 10; ++exponent) {
        SCOPED_TRACE(exponent);
        step(true);
        expect_scale(exponent);
      }
      expect_endpoint_no_op(true, 10);
      ASSERT_TRUE(editor.history().undo().ok());
      panel.refresh_texture_settings();
      expect_scale(9);
      ASSERT_TRUE(editor.history().redo().ok());
      panel.refresh_texture_settings();
      expect_scale(10);

      for (int exponent = 9; exponent >= -13; --exponent) {
        SCOPED_TRACE(exponent);
        step(false);
        expect_scale(exponent);
      }
      expect_endpoint_no_op(false, -13);
      ASSERT_TRUE(editor.history().undo().ok());
      panel.refresh_texture_settings();
      expect_scale(-12);
      ASSERT_TRUE(editor.history().redo().ok());
      panel.refresh_texture_settings();
      expect_scale(-13);
      panel.unbind();
    }
  }
}

TEST(QtToolSettingsPanelTests,
     MixedTextureScaleSpinnerArrowsStartFromNeutralAndUndoRestoresMixedValues) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-texture-mixed-scale-step-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  using Field = quader::QuaderFaceTextureField;
  const std::array<std::pair<const char *, Field>, 2> axes{{
      {"x", Field::ScaleX}, {"y", Field::ScaleY},
  }};
  for (const auto &[axis, field] : axes) {
    SCOPED_TRACE(axis);
    auto standalone = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    const auto created = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        "Mixed Texture Scale Steps");
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
    ASSERT_TRUE(set_face_texture_field(editor, field, 4.0).ok());
    ASSERT_TRUE(editor.selection().select_face(created.value(), 2).ok());
    ASSERT_TRUE(set_face_texture_field(editor, field, 8.0).ok());
    ASSERT_TRUE(editor.selection().select_faces(created.value(), {1, 2}).ok());
    quader::QtToolSettingsPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    auto *spin = panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("tool_settings.texture.scale_%1")
            .arg(QString::fromLatin1(axis)));
    ASSERT_NE(spin, nullptr);
    for (const bool upward : {true, false}) {
      SCOPED_TRACE(upward ? "up" : "down");
      ASSERT_EQ(spin->text(), QStringLiteral("--"));
      if (upward) {
        spin->stepUp();
      } else {
        spin->stepDown();
      }
      finish_spin_edit(*spin);
      const auto settings = editor.modeling().face_texture_settings();
      EXPECT_EQ(field == Field::ScaleX ? settings.scale_x : settings.scale_y,
                std::optional<double>(upward ? 2.0 : 0.5));
      EXPECT_EQ(spin->text(), upward ? QStringLiteral("2.0000")
                                    : QStringLiteral("0.5000"));
      ASSERT_TRUE(editor.history().undo().ok());
      panel.refresh_texture_settings();
      EXPECT_EQ(spin->text(), QStringLiteral("--"));
      ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
      auto restored = editor.modeling().face_texture_settings();
      EXPECT_EQ(field == Field::ScaleX ? restored.scale_x : restored.scale_y,
                std::optional<double>(4.0));
      ASSERT_TRUE(editor.selection().select_face(created.value(), 2).ok());
      restored = editor.modeling().face_texture_settings();
      EXPECT_EQ(field == Field::ScaleX ? restored.scale_x : restored.scale_y,
                std::optional<double>(8.0));
      ASSERT_TRUE(editor.selection().select_faces(created.value(), {1, 2}).ok());
      panel.refresh_texture_settings();
    }
    panel.unbind();
  }
}

TEST(QtToolSettingsPanelTests,
     TextureScaleManualValuesRemainUnrestrictedAndArrowsPreserveNegativeSigns) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-texture-manual-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  for (const bool x_axis : {true, false}) {
    SCOPED_TRACE(x_axis ? "x" : "y");
    auto *spin = panel.findChild<QDoubleSpinBox *>(
        x_axis ? QStringLiteral("tool_settings.texture.scale_x")
               : QStringLiteral("tool_settings.texture.scale_y"));
    ASSERT_NE(spin, nullptr);
    const auto stored_value = [&] {
      const auto settings = editor.modeling().face_texture_settings();
      return x_axis ? settings.scale_x : settings.scale_y;
    };
    type_spin_text(*spin, QStringLiteral("2048"));
    send_key_event(*spin, Qt::Key_Return);
    EXPECT_EQ(stored_value(), std::optional<double>(2048.0));
    EXPECT_EQ(spin->text(), QStringLiteral("2048.0000"));
    type_spin_text(*spin, QStringLiteral("0.0001"));
    send_key_event(*spin, Qt::Key_Return);
    ASSERT_TRUE(stored_value().has_value());
    EXPECT_NEAR(*stored_value(), 0.0001, 1.0e-11);
    EXPECT_EQ(spin->text(), QStringLiteral("0.0001"));
    type_spin_text(*spin, QStringLiteral("-1.5"));
    send_key_event(*spin, Qt::Key_Return);
    EXPECT_EQ(stored_value(), std::optional<double>(-1.5));
    spin->stepUp();
    finish_spin_edit(*spin);
    EXPECT_EQ(stored_value(), std::optional<double>(-3.0));
    EXPECT_EQ(spin->text(), QStringLiteral("-3.0000"));
    spin->stepDown();
    finish_spin_edit(*spin);
    EXPECT_EQ(stored_value(), std::optional<double>(-1.5));
    EXPECT_EQ(spin->text(), QStringLiteral("-1.5000"));
  }
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     EditTextureActionsFlushValidDraftsAndRejectInvalidStaleAndFailedEdits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-edit-texture-draft-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Edit Texture Drafts");
  ASSERT_TRUE(created.ok());
  std::vector<quader::QuaderNotification> notifications;
  editor.ui().set_runtime_hooks({.notify = [&](quader::QuaderNotification value) {
    notifications.push_back(std::move(value));
    return quader::QuaderApiVoidResult{.ok = true};
  }});
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *scale = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_x"));
  auto *up = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.edit_texture.scale_x_up"));
  auto *down = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.edit_texture.scale_x_down"));
  auto *flip = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.edit_texture.flip_face_horizontal"));
  auto *message = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.edit_texture.message"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(up, nullptr);
  ASSERT_NE(down, nullptr);
  ASSERT_NE(flip, nullptr);
  ASSERT_NE(message, nullptr);
  auto *error_message = dynamic_cast<quader::QtStatusMessage *>(message);
  ASSERT_NE(error_message, nullptr);
  int inline_announcements = 0;
  error_message->set_error_sound_callback([&] { ++inline_announcements; });

  type_spin_text(*scale, QStringLiteral("2.0"));
  ASSERT_TRUE(panel.numeric_interaction_active());
  up->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(4.0));
  EXPECT_FALSE(panel.numeric_interaction_active());
  process_events_for(75);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(4.0));
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(2.0));
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh_texture_settings();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  ASSERT_TRUE(editor.history().can_redo());

  // A real mouse click must not let Qt silently restore zero to the previous
  // value on focus-out and then execute an action the user could not see.
  EXPECT_TRUE(notifications.empty());
  type_spin_text(*scale, QStringLiteral("0"));
  const auto invalid_revision = editor.revisions().modeling_content_revision;
  send_mouse_event(*up, QEvent::MouseButtonPress, up->rect().center(),
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*up, QEvent::MouseButtonRelease, up->rect().center(),
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(editor.history().can_redo());
  EXPECT_TRUE(panel.numeric_interaction_active());
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(message->text().isEmpty());
  EXPECT_TRUE(error_message->is_error());
  ASSERT_EQ(notifications.size(), 1u);
  EXPECT_EQ(notifications.back().level, quader::QuaderNotificationLevel::Error);
  EXPECT_EQ(notifications.back().message, message->text().toStdString());
  panel.refresh_texture_settings();
  process_events_for(75);
  EXPECT_EQ(notifications.size(), 1u);
  EXPECT_EQ(inline_announcements, 0);
  type_spin_text(*scale, QStringLiteral("1"));
  // The correction is dispatched by a timer even though the stored value is
  // unchanged. Wait for its observable completion, not another timer's expiry.
  ASSERT_TRUE(process_events_until([message] { return message->isHidden(); },
                                  1000))
      << message->text().toStdString();
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(editor.history().can_redo());
  type_spin_text(*scale, QStringLiteral("-"));
  send_mouse_event(*up, QEvent::MouseButtonPress, up->rect().center(),
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*up, QEvent::MouseButtonRelease, up->rect().center(),
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(editor.history().can_redo());
  EXPECT_FALSE(message->isHidden());
  type_spin_text(*scale, QStringLiteral("3.0"));
  up->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(6.0));
  EXPECT_TRUE(message->isHidden());

  scale->setValue(7.0);
  ASSERT_TRUE(editor.selection().select_face(created.value(), 2).ok());
  const auto stale_revision = editor.revisions().modeling_content_revision;
  up->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, stale_revision);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_FALSE(message->isHidden());
  process_events_for(75);
  EXPECT_EQ(editor.revisions().modeling_content_revision, stale_revision);
  // The rejected click refreshed the presentation, so a deliberate second
  // click now belongs to the newly selected face.
  up->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(2.0));

  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleX,
                  1.0 / 8192.0).ok());
  panel.refresh_texture_settings();
  const auto failure_revision = editor.revisions().modeling_content_revision;
  const auto failure_history = editor.history().snapshot();
  down->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, failure_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, failure_history.undo_label);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0 / 8192.0));
  EXPECT_TRUE(message->isHidden());
  const auto notifications_before_failure = notifications.size();
  flip->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, failure_revision);
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(message->text().isEmpty());
  EXPECT_TRUE(error_message->is_error());
  ASSERT_EQ(notifications.size(), notifications_before_failure + 1u);
  EXPECT_EQ(notifications.back().level, quader::QuaderNotificationLevel::Error);
  EXPECT_EQ(notifications.back().message, message->text().toStdString());
  panel.refresh_texture_settings();
  process_events_for(75);
  EXPECT_EQ(notifications.size(), notifications_before_failure + 1u);
  EXPECT_EQ(inline_announcements, 0);

  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  panel.refresh_texture_settings();
  EXPECT_FALSE(up->isEnabled());
  EXPECT_FALSE(flip->isEnabled());
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(error_message->is_error());
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  panel.refresh_texture_settings();
  EXPECT_TRUE(up->isEnabled());
  EXPECT_TRUE(message->isHidden());
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     EditTexturePreferencesAndLocalePreserveDraftsFoldsAndClosedDocks) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-edit-texture-presentation-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  double rotation_step = 15.0;
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  editor.preferences().set_runtime_hooks(quader::QuaderPreferencesRuntimeHooks{
      .rotation_snap_enabled = [] { return false; },
      .rotation_snap_degrees = [&rotation_step] { return rotation_step; },
  });
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());

  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.resize(600, 1000);
  window.setCentralWidget(new QWidget(&window));
  auto *dock = new QDockWidget(&window);
  auto *panel = new quader::QtToolSettingsPanel(dock);
  panel->setFixedWidth(panel->minimumWidth());
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  panel->bind(editor);
  window.show();
  QCoreApplication::processEvents();
  auto *section = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.section.edit_texture"));
  auto *content = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.section.edit_texture.content"));
  auto *header = panel->findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.edit_texture.header"));
  auto *scale = panel->findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_x"));
  auto *rotate = panel->findChild<QPushButton *>(
      QStringLiteral("tool_settings.edit_texture.rotate_clockwise"));
  auto *treat_as_one = panel->findChild<QCheckBox *>(
      QStringLiteral("tool_settings.edit_texture.treat_as_one"));
  ASSERT_NE(section, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(rotate, nullptr);
  ASSERT_NE(treat_as_one, nullptr);
  const auto buttons = section->findChildren<QPushButton *>();
  ASSERT_EQ(buttons.size(), 25);
  for (QPushButton *button : buttons) {
    EXPECT_TRUE(content->rect().contains(button->geometry()));
    EXPECT_GE(button->width(), button->fontMetrics().horizontalAdvance(
                                  button->text()) + 12)
        << button->objectName().toStdString();
  }

  type_spin_text(*scale, QStringLiteral("4.0"));
  const auto before_toggle = editor.revisions().modeling_content_revision;
  const auto before_toggle_history = editor.history().snapshot();
  treat_as_one->click();
  EXPECT_TRUE(treat_as_one->isChecked());
  EXPECT_EQ(editor.revisions().modeling_content_revision, before_toggle);
  EXPECT_EQ(editor.history().snapshot().undo_label, before_toggle_history.undo_label);
  EXPECT_EQ(scale->text(), QStringLiteral("4.0"));
  EXPECT_TRUE(panel->numeric_interaction_active());
  ASSERT_TRUE(process_events_until([&] {
    return editor.modeling().face_texture_settings().scale_x ==
           std::optional<double>(4.0);
  }, 1000));
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(4.0));
  // A context-only toggle after the live numeric publication must preserve
  // that gesture too, so its next accepted value stays in the same edit.
  treat_as_one->click();
  EXPECT_FALSE(treat_as_one->isChecked());
  EXPECT_TRUE(panel->numeric_interaction_active());
  type_spin_text(*scale, QStringLiteral("5.0"));
  rotation_step = 22.5;
  panel->refresh_texture_settings();
  EXPECT_EQ(scale->text(), QStringLiteral("5.0"));
  EXPECT_TRUE(rotate->toolTip().contains(QStringLiteral("22.5")));
  ASSERT_TRUE(editor.preferences().set_shortcut(
                  quader::quader_command::kModelingTextureRotateClockwise,
                  "Ctrl+Alt+Shift+F12").ok());
  panel->refresh_texture_settings();
  EXPECT_TRUE(rotate->toolTip().contains(QStringLiteral("F12")));
  EXPECT_EQ(scale->text(), QStringLiteral("5.0"));
  rotate->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(5.0));
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().rotation_degrees, 22.5);
  EXPECT_FALSE(panel->numeric_interaction_active());
  process_events_for(75);
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().rotation_degrees, 22.5);

  TexturePresentationEvents presentation_events;
  for (QPushButton *button : buttons) {
    button->installEventFilter(&presentation_events);
  }
  for (int frame = 0; frame < 20; ++frame) {
    panel->refresh_texture_settings();
  }
  EXPECT_EQ(presentation_events.changes, 0);

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "it-IT"));
  const QPointer<QPushButton> retained_rotate(rotate);
  panel->set_localization(italian);
  QCoreApplication::processEvents();
  EXPECT_EQ(header->text(), QStringLiteral("Modifica texture"));
  EXPECT_EQ(rotate->text(), QStringLiteral("Orario"));
  EXPECT_EQ(rotate->accessibleName(), QStringLiteral("Ruota texture in senso orario"));
  EXPECT_TRUE(rotate->toolTip().contains(QStringLiteral("F12")));
  EXPECT_EQ(retained_rotate.data(), rotate);
  for (QPushButton *button : buttons) {
    EXPECT_GE(button->width(), button->fontMetrics().horizontalAdvance(
                                  button->text()) + 12)
        << button->objectName().toStdString();
  }
  header->click();
  EXPECT_TRUE(content->isHidden());
  dock->close();
  ASSERT_TRUE(editor.selection().clear().ok());
  panel->refresh_texture_settings();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  panel->refresh_texture_settings();
  EXPECT_TRUE(dock->isHidden());
  EXPECT_FALSE(section->isHidden());
  EXPECT_TRUE(content->isHidden());
  EXPECT_FALSE(header->isChecked());
  panel->unbind();
}

TEST(QtToolSettingsPanelTests,
     UvPeelSectionRetainsSceneOptionsFoldsLocaleAndClosedDockState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-uv-peel-section-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());

  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.resize(600, 900);
  window.setCentralWidget(new QWidget(&window));
  auto *dock = new QDockWidget(&window);
  auto *panel = new quader::QtToolSettingsPanel(dock);
  panel->setFixedWidth(panel->minimumWidth());
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  panel->bind(editor);
  window.show();
  QCoreApplication::processEvents();
  auto *section = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.section.uv_peel"));
  auto *header = panel->findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.uv_peel.header"));
  auto *content = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.section.uv_peel.content"));
  auto *face_section = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.section.texture"));
  auto *target_title = panel->findChild<QLabel *>(
      QStringLiteral("tool_settings.target_title"));
  auto *title_separator = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.title_separator"));
  auto *axis = panel->findChild<QComboBox *>(
      QStringLiteral("tool_settings.uv_peel.axis"));
  auto *world_space = panel->findChild<QCheckBox *>(
      QStringLiteral("tool_settings.uv_peel.world_space"));
  auto *apply = panel->findChild<QPushButton *>(
      QStringLiteral("tool_settings.uv_peel.apply"));
  auto *help = panel->findChild<QLabel *>(
      QStringLiteral("tool_settings.uv_peel.help"));
  ASSERT_NE(section, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(face_section, nullptr);
  ASSERT_NE(target_title, nullptr);
  ASSERT_NE(title_separator, nullptr);
  ASSERT_NE(axis, nullptr);
  ASSERT_NE(world_space, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(help, nullptr);
  EXPECT_TRUE(section->isHidden());
  EXPECT_TRUE(header->isChecked());
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_FALSE(title_separator->isHidden());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  panel->refresh();
  EXPECT_TRUE(section->isHidden());
  EXPECT_FALSE(face_section->isHidden());
  EXPECT_TRUE(target_title->isHidden());
  EXPECT_TRUE(title_separator->isHidden());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  panel->refresh();
  QCoreApplication::processEvents();
  EXPECT_FALSE(section->isHidden());
  EXPECT_TRUE(face_section->isHidden());
  EXPECT_FALSE(panel->has_active_target());
  EXPECT_TRUE(target_title->isHidden());
  EXPECT_TRUE(title_separator->isHidden());
  EXPECT_EQ(axis->currentIndex(), 0);
  EXPECT_TRUE(world_space->isChecked());
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_FALSE(help->text().isEmpty());

  constexpr std::array<const char *, 4> kFields{
      "u_repeat", "v_repeat", "u_offset", "v_offset"};
  constexpr std::array<double, 4> kValues{2.0, 3.0, 0.25, -0.5};
  std::array<QDoubleSpinBox *, 4> spins{};
  const auto before = editor.revisions();
  const auto before_history = editor.history().snapshot();
  axis->setCurrentIndex(1);
  world_space->click();
  for (std::size_t index = 0; index < kFields.size(); ++index) {
    spins[index] = panel->findChild<QDoubleSpinBox *>(
        QStringLiteral("tool_settings.uv_peel.%1").arg(
            QString::fromLatin1(kFields[index])));
    ASSERT_NE(spins[index], nullptr);
    EXPECT_DOUBLE_EQ(spins[index]->value(), index < 2 ? 1.0 : 0.0);
    spins[index]->setValue(kValues[index]);
  }
  const quader::QuaderUvPeelOptions expected{
      .axis = quader::QuaderUvPeelAxis::V, .world_space = false,
      .u_repeat = 2.0, .v_repeat = 3.0, .u_offset = 0.25, .v_offset = -0.5};
  ASSERT_TRUE(process_events_until([&] {
    return editor.modeling().uv_peel_settings().options == expected;
  }, 1000));
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, expected);
  EXPECT_EQ(editor.revisions().modeling_content_revision, before.modeling_content_revision);
  EXPECT_EQ(editor.revisions().scene_revision, before.scene_revision);
  EXPECT_EQ(editor.revisions().asset_revision, before.asset_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, before_history.undo_label);
  for (QDoubleSpinBox *spin : spins) {
    finish_spin_edit(*spin);
  }
  EXPECT_FALSE(panel->numeric_interaction_active());

  TexturePresentationEvents presentation_events;
  for (QWidget *widget : section->findChildren<QWidget *>()) {
    widget->installEventFilter(&presentation_events);
  }
  for (int frame = 0; frame < 20; ++frame) {
    panel->refresh_uv_peel_settings();
  }
  EXPECT_EQ(presentation_events.changes, 0);
  ASSERT_TRUE(editor.preferences().set_shortcut(
                  quader::quader_command::kModelingUvPeel,
                  "Ctrl+Alt+Shift+F11").ok());
  panel->refresh_uv_peel_settings();
  EXPECT_TRUE(apply->toolTip().contains(QStringLiteral("F11")));

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "it-IT"));
  const QPointer<QDoubleSpinBox> retained_repeat(spins[0]);
  panel->set_localization(italian);
  QCoreApplication::processEvents();
  EXPECT_EQ(header->text(), QStringLiteral("Distendi UV"));
  EXPECT_EQ(axis->accessibleName(), QStringLiteral("Asse"));
  EXPECT_EQ(world_space->text(), QStringLiteral("Spazio globale"));
  EXPECT_EQ(spins[0]->accessibleName(), QStringLiteral("Ripetizioni U"));
  EXPECT_EQ(apply->accessibleName(), QStringLiteral("Distendi UV"));
  EXPECT_TRUE(apply->toolTip().contains(QStringLiteral("F11")));
  EXPECT_EQ(retained_repeat.data(), spins[0]);
  for (QDoubleSpinBox *spin : spins) {
    EXPECT_TRUE(content->rect().contains(spin->geometry()));
    EXPECT_EQ(spin->locale().decimalPoint(), QStringLiteral("."));
  }
  EXPECT_TRUE(content->rect().contains(axis->geometry()));
  EXPECT_GE(apply->width(), apply->fontMetrics().horizontalAdvance(apply->text()) + 12);
  header->click();
  dock->close();
  ASSERT_TRUE(editor.selection().clear().ok());
  panel->refresh_uv_peel_settings();
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_FALSE(title_separator->isHidden());
  EXPECT_TRUE(section->isHidden());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 2, 3).ok());
  panel->refresh_uv_peel_settings();
  EXPECT_TRUE(dock->isHidden());
  EXPECT_FALSE(section->isHidden());
  EXPECT_TRUE(content->isHidden());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, expected);
  EXPECT_TRUE(target_title->isHidden());
  EXPECT_TRUE(title_separator->isHidden());
  panel->unbind();
  EXPECT_FALSE(target_title->isHidden());
  EXPECT_FALSE(title_separator->isHidden());
}

TEST(QtToolSettingsPanelTests,
     UvPeelButtonFlushesAllStagedOptionsAndMatchesPublicApiWithOneUndo) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-uv-peel-binding-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  constexpr std::array<quader::QuaderUvPeelAxis, 2> kAxes{
      quader::QuaderUvPeelAxis::U, quader::QuaderUvPeelAxis::V};
  for (const auto selected_axis : kAxes) {
    for (bool world_space : {false, true}) {
      SCOPED_TRACE(static_cast<int>(selected_axis));
      SCOPED_TRACE(world_space);
      auto standalone = quader::create_standalone_editor();
      auto reference = quader::create_standalone_editor();
      auto &editor = standalone.editor();
      auto &expected_editor = reference.editor();
      const auto created = create_texture_action_surface(editor);
      const auto expected_created = create_texture_action_surface(expected_editor);
      ASSERT_TRUE(created.ok());
      ASSERT_TRUE(expected_created.ok());
      ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
      ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
      ASSERT_TRUE(expected_editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
      ASSERT_TRUE(expected_editor.selection().select_edge(expected_created.value(), 1, 2).ok());
      const auto before = editor.modeling().mesh(created.value()).compiled_mesh();
      ASSERT_TRUE(before.has_value());
      quader::QtToolSettingsPanel panel;
      panel.setAttribute(Qt::WA_DontShowOnScreen, true);
      panel.bind(editor);
      panel.show();
      auto *axis = panel.findChild<QComboBox *>(
          QStringLiteral("tool_settings.uv_peel.axis"));
      auto *world = panel.findChild<QCheckBox *>(
          QStringLiteral("tool_settings.uv_peel.world_space"));
      auto *apply = panel.findChild<QPushButton *>(
          QStringLiteral("tool_settings.uv_peel.apply"));
      ASSERT_NE(axis, nullptr);
      ASSERT_NE(world, nullptr);
      ASSERT_NE(apply, nullptr);
      axis->setCurrentIndex(selected_axis == quader::QuaderUvPeelAxis::U ? 0 : 1);
      world->setChecked(world_space);
      constexpr std::array<const char *, 4> kFields{
          "u_repeat", "v_repeat", "u_offset", "v_offset"};
      constexpr std::array<double, 4> kValues{1.75, 2.25, 0.3, -0.2};
      for (std::size_t index = 0; index < kFields.size(); ++index) {
        auto *spin = panel.findChild<QDoubleSpinBox *>(
            QStringLiteral("tool_settings.uv_peel.%1").arg(
                QString::fromLatin1(kFields[index])));
        ASSERT_NE(spin, nullptr);
        spin->setValue(kValues[index]);
      }
      const quader::QuaderUvPeelOptions options{
          .axis = selected_axis, .world_space = world_space,
          .u_repeat = 1.75, .v_repeat = 2.25, .u_offset = 0.3, .v_offset = -0.2};
      const auto expected_result = expected_editor.modeling().peel_uv(
          {.options = options,
           .selection_token = expected_editor.modeling()
                                  .uv_peel_settings().selection_token});
      ASSERT_TRUE(expected_result.ok()) << expected_result.error_message();
      ASSERT_TRUE(expected_result.changed());
      ASSERT_TRUE(panel.numeric_interaction_active());
      apply->click();
      EXPECT_FALSE(panel.numeric_interaction_active());
      EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
      const auto actual = editor.modeling().mesh(created.value()).compiled_mesh();
      const auto expected = expected_editor.modeling()
                                .mesh(expected_created.value()).compiled_mesh();
      ASSERT_TRUE(actual.has_value());
      ASSERT_TRUE(expected.has_value());
      expect_same_texture_mesh(*actual, *expected);
      EXPECT_EQ(editor.history().snapshot().undo_label, "UV Peel");
      const auto completed_revision = editor.revisions().modeling_content_revision;
      process_events_for(75);
      EXPECT_EQ(editor.revisions().modeling_content_revision, completed_revision);
      ASSERT_TRUE(editor.history().undo().ok());
      const auto restored = editor.modeling().mesh(created.value()).compiled_mesh();
      ASSERT_TRUE(restored.has_value());
      expect_same_texture_mesh(*restored, *before);
      EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
      panel.unbind();
    }
  }
}

TEST(QtToolSettingsPanelTests,
     UvPeelRejectsInvalidStalePreviewAndUnsupportedChartsWithoutMutation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-uv-peel-validation-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  std::vector<quader::QuaderNotification> notifications;
  editor.ui().set_runtime_hooks({.notify = [&](quader::QuaderNotification value) {
    notifications.push_back(std::move(value));
    return quader::QuaderApiVoidResult{.ok = true};
  }});
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.uv_peel.u_repeat"));
  auto *world_space = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.uv_peel.world_space"));
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.uv_peel.apply"));
  auto *section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.uv_peel"));
  auto *message = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.uv_peel.message"));
  ASSERT_NE(repeat, nullptr);
  ASSERT_NE(world_space, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(section, nullptr);
  ASSERT_NE(message, nullptr);
  auto *error_message = dynamic_cast<quader::QtStatusMessage *>(message);
  ASSERT_NE(error_message, nullptr);
  int inline_announcements = 0;
  error_message->set_error_sound_callback([&] { ++inline_announcements; });
  const auto before = editor.revisions().modeling_content_revision;
  const auto before_history = editor.history().snapshot();
  for (const QString &invalid : {QStringLiteral("0"), QStringLiteral("-")}) {
    const auto announcements_before = notifications.size();
    type_spin_text(*repeat, invalid);
    send_mouse_event(*apply, QEvent::MouseButtonPress, apply->rect().center(),
                     Qt::LeftButton, Qt::LeftButton);
    send_mouse_event(*apply, QEvent::MouseButtonRelease, apply->rect().center(),
                     Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(editor.revisions().modeling_content_revision, before);
    EXPECT_EQ(editor.modeling().uv_peel_settings().options,
              quader::QuaderUvPeelOptions{});
    EXPECT_EQ(editor.history().snapshot().undo_label, before_history.undo_label);
    EXPECT_FALSE(message->isHidden());
    EXPECT_TRUE(panel.numeric_interaction_active());
    EXPECT_TRUE(error_message->is_error());
    ASSERT_EQ(notifications.size(), announcements_before + 1u);
    EXPECT_EQ(notifications.back().level, quader::QuaderNotificationLevel::Error);
    EXPECT_EQ(notifications.back().message, message->text().toStdString());
  }
  for (int toggle = 0; toggle < 2; ++toggle) {
    const QPoint indicator(7, world_space->height() / 2);
    send_mouse_event(*world_space, QEvent::MouseButtonPress, indicator,
                     Qt::LeftButton, Qt::LeftButton);
    send_mouse_event(*world_space, QEvent::MouseButtonRelease, indicator,
                     Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(world_space->isChecked(), toggle != 0);
  }
  process_events_for(125);
  auto *repeat_text = repeat->findChild<QLineEdit *>();
  ASSERT_NE(repeat_text, nullptr);
  EXPECT_EQ(repeat_text->text(), QStringLiteral("-"));
  EXPECT_EQ(editor.modeling().uv_peel_settings().options,
            quader::QuaderUvPeelOptions{});
  EXPECT_TRUE(panel.numeric_interaction_active());
  // Accepting a correction to the stored value clears the validation message
  // even when the owner need not advance its settings revision.
  type_spin_text(*repeat, QStringLiteral("1"));
  ASSERT_TRUE(process_events_until([&] { return message->isHidden(); }, 1000))
      << message->text().toStdString();
  EXPECT_TRUE(message->isHidden());
  finish_spin_edit(*repeat);
  repeat->setValue(3.0);
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 2, 3).ok());
  apply->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, before);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options,
            quader::QuaderUvPeelOptions{});
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(panel.numeric_interaction_active());
  process_events_for(125);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 1.0);

  repeat->setValue(2.0);
  quader::QuaderUvPeelOptions external_options;
  external_options.u_repeat = 5.0;
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(external_options).ok());
  apply->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, before);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, external_options);
  EXPECT_DOUBLE_EQ(repeat->value(), 5.0);
  EXPECT_FALSE(message->isHidden());

  repeat->setValue(3.0);
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  panel.refresh_uv_peel_settings();
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(repeat->isEnabled());
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_FALSE(message->isHidden());
  process_events_for(125);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, external_options);
  EXPECT_FALSE(error_message->is_error());
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  panel.refresh_uv_peel_settings();
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(message->isHidden());

  const auto box = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Branched UV Guide");
  ASSERT_TRUE(box.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(box.value(), 1, 2).ok());
  ASSERT_TRUE(editor.selection().select_edge(
                  box.value(), 1, 4, quader::QuaderSelectionEdit::Add).ok());
  ASSERT_TRUE(editor.selection().select_edge(
                  box.value(), 1, 5, quader::QuaderSelectionEdit::Add).ok());
  panel.refresh_uv_peel_settings();
  const auto branched_before = editor.modeling().mesh(box.value()).compiled_mesh();
  ASSERT_TRUE(branched_before.has_value());
  const auto branched_revision = editor.revisions().modeling_content_revision;
  const auto branched_history = editor.history().snapshot();
  repeat->setValue(7.0);
  const auto announcements_before = notifications.size();
  apply->click();
  quader::QuaderUvPeelOptions accepted_options = external_options;
  accepted_options.u_repeat = 7.0;
  EXPECT_EQ(editor.revisions().modeling_content_revision, branched_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, branched_history.undo_label);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, accepted_options);
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(message->text().isEmpty());
  EXPECT_TRUE(error_message->is_error());
  ASSERT_EQ(notifications.size(), announcements_before + 1u);
  EXPECT_EQ(notifications.back().level, quader::QuaderNotificationLevel::Error);
  EXPECT_EQ(notifications.back().message, message->text().toStdString());
  panel.refresh_uv_peel_settings();
  process_events_for(125);
  EXPECT_EQ(notifications.size(), announcements_before + 1u);
  EXPECT_EQ(inline_announcements, 0);
  const auto branched_after = editor.modeling().mesh(box.value()).compiled_mesh();
  ASSERT_TRUE(branched_after.has_value());
  expect_same_texture_mesh(*branched_after, *branched_before);
  repeat->setValue(6.0);

  auto replacement = quader::create_standalone_editor();
  auto &replacement_editor = replacement.editor();
  const auto replacement_surface = create_texture_action_surface(replacement_editor);
  ASSERT_TRUE(replacement_surface.ok());
  ASSERT_TRUE(replacement_editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(replacement_editor.selection()
                  .select_edge(replacement_surface.value(), 1, 2).ok());
  quader::QuaderUvPeelOptions replacement_options;
  replacement_options.u_repeat = 9.0;
  ASSERT_TRUE(replacement_editor.modeling()
                  .set_uv_peel_options(replacement_options).ok());
  panel.bind(replacement_editor);
  process_events_for(75);
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, accepted_options);
  EXPECT_EQ(replacement_editor.modeling().uv_peel_settings().options,
            replacement_options);
  EXPECT_DOUBLE_EQ(repeat->value(), 9.0);
  finish_spin_edit(*repeat);
  repeat->setValue(10.0);
  panel.unbind();
  process_events_for(75);
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, accepted_options);
  EXPECT_EQ(replacement_editor.modeling().uv_peel_settings().options,
            replacement_options);
}

TEST(QtToolSettingsPanelTests,
     FaceTextureRotationUsesSelectionBaselineAndCoalescesOneNumericGesture) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-face-texture-rotation-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Texture Rotation Faces");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::Rotate, 30.0).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::Rotate, -15.0).ok());
  ASSERT_TRUE(editor.selection().select_faces(source.value(), {1, 2}).ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *rotate = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.rotate"));
  auto *scale_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_x"));
  ASSERT_NE(rotate, nullptr);
  ASSERT_NE(scale_x, nullptr);
  EXPECT_DOUBLE_EQ(rotate->value(), 0.0);

  rotate->setValue(10.0);
  ASSERT_TRUE(process_events_until([&] {
    return editor.modeling().face_texture_settings().rotation_degrees == 10.0;
  }, 1000));
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().rotation_degrees,
                   10.0);
  rotate->setValue(20.0);
  ASSERT_TRUE(process_events_until([&] {
    return editor.modeling().face_texture_settings().rotation_degrees == 20.0;
  }, 1000));
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().rotation_degrees,
                   20.0);
  finish_spin_edit(*rotate);
  EXPECT_FALSE(panel.numeric_interaction_active());
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh_texture_settings();
  EXPECT_DOUBLE_EQ(rotate->value(), 0.0);
  ASSERT_TRUE(editor.history().redo().ok());
  panel.refresh_texture_settings();

  // A subsequent gesture is separate; one undo cannot consume both gestures.
  scale_x->setValue(2.0);
  finish_spin_edit(*scale_x);
  scale_x->setValue(3.0);
  finish_spin_edit(*scale_x);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh_texture_settings();
  EXPECT_DOUBLE_EQ(scale_x->value(), 2.0);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh_texture_settings();
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);

  ASSERT_TRUE(editor.selection().select_face(
                  source.value(), 3, quader::QuaderSelectionEdit::Add).ok());
  panel.refresh_texture_settings();
  EXPECT_DOUBLE_EQ(rotate->value(), 0.0);
  rotate->setValue(8.0);
  finish_spin_edit(*rotate);
  ASSERT_TRUE(editor.selection().select_face(
                  source.value(), 3, quader::QuaderSelectionEdit::Remove).ok());
  panel.refresh_texture_settings();
  EXPECT_DOUBLE_EQ(rotate->value(), 0.0);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     FaceTextureQueuedEditsAreDiscardedAcrossSelectionPreviewAndRebinding) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-face-texture-lifecycle-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Texture Lifecycle Faces");
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *scale_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.scale_x"));
  auto *shift_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.texture.shift_x"));
  auto *reason = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.texture.disabled_reason"));
  ASSERT_NE(scale_x, nullptr);
  ASSERT_NE(shift_x, nullptr);
  ASSERT_NE(reason, nullptr);

  shift_x->setValue(64.0);
  EXPECT_TRUE(panel.numeric_interaction_active());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  const auto unchanged_revision = editor.revisions().modeling_content_revision;
  // Even without a panel refresh, the delayed callback validates its owner key.
  process_events_for(75);
  EXPECT_EQ(editor.revisions().modeling_content_revision, unchanged_revision);
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().shift_x, 0.0);
  EXPECT_FALSE(panel.numeric_interaction_active());

  scale_x->setValue(7.0);
  ASSERT_TRUE(editor.selection().select_face(
                  source.value(), 1, quader::QuaderSelectionEdit::Add).ok());
  panel.refresh_texture_settings();
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);
  process_events_for(75);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));

  scale_x->setValue(0.0);
  finish_spin_edit(*scale_x);
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);
  EXPECT_EQ(editor.revisions().modeling_content_revision, unchanged_revision);

  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  panel.refresh_texture_settings();
  EXPECT_FALSE(scale_x->isEnabled());
  EXPECT_FALSE(reason->isHidden());
  EXPECT_FALSE(reason->text().isEmpty());
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  panel.refresh_texture_settings();
  EXPECT_TRUE(scale_x->isEnabled());
  EXPECT_TRUE(reason->isHidden());

  scale_x->setValue(9.0);
  EXPECT_TRUE(panel.numeric_interaction_active());
  panel.unbind();
  process_events_for(75);
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));

  panel.bind(editor);
  scale_x->setValue(11.0);
  auto replacement = quader::create_standalone_editor();
  auto &replacement_editor = replacement.editor();
  const auto replacement_source = replacement_editor.modeling().create_box(
      quader::QuaderBounds{.min = {-1.0f, -1.0f, -1.0f},
                           .max = {1.0f, 1.0f, 1.0f}},
      "Replacement Texture Scene");
  ASSERT_TRUE(replacement_source.ok());
  ASSERT_TRUE(replacement_editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(replacement_editor.selection()
                  .select_face(replacement_source.value(), 1).ok());
  panel.bind(replacement_editor);
  process_events_for(75);
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  EXPECT_EQ(replacement_editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     DynamicResetAvailabilityUpdatesWithoutRebuildingSettingsControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-reset-availability-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *subdivisions = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.box.subdivisions_x"));
  auto *reset = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.reset_defaults"));
  ASSERT_NE(subdivisions, nullptr);
  ASSERT_NE(reset, nullptr);
  QPointer<QSpinBox> retained_subdivisions(subdivisions);
  EXPECT_TRUE(reset->isEnabled());

  const quader::ModelingBoxPreview footprint =
      session.modeling().begin_box_footprint({
          .screen_position = {400.0f, 300.0f},
          .ray = {.origin = {0.0f, 4.0f, 0.0f},
                  .direction = {0.0f, -1.0f, 0.0f}},
          .grid_size = 1.0,
      });
  ASSERT_TRUE(footprint.ok) << footprint.message;
  ASSERT_EQ(footprint.drag_state, quader::ModelingBoxDragState::Footprint);
  panel.refresh();
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.box.subdivisions_x")),
            retained_subdivisions.data());
  EXPECT_FALSE(reset->isEnabled());

  const quader::ModelingCommandResult canceled =
      session.modeling().cancel_box();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  panel.refresh();
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.box.subdivisions_x")),
            retained_subdivisions.data());
  EXPECT_TRUE(reset->isEnabled());
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     CurvePresetDropdownReusesGenericEditorsAndItalianLocalization) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-curve-preset-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "it-IT"));
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_localization(italian);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.curve.type"));
  auto *type_label = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.control.curve.type.label"));
  auto *closed = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.curve.closed"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(type_label, nullptr);
  ASSERT_NE(closed, nullptr);
  EXPECT_EQ(type_label->text(), QStringLiteral("Tipo di curva"));
  EXPECT_EQ(closed->text(), QStringLiteral("Chiusa"));
  ASSERT_EQ(type->count(), 3);
  EXPECT_EQ(type->itemText(type->findData(QStringLiteral("draw"))),
            QStringLiteral("Disegna"));
  EXPECT_EQ(type->itemText(type->findData(QStringLiteral("circle"))),
            QStringLiteral("Cerchio"));
  EXPECT_EQ(type->itemText(type->findData(QStringLiteral("oval"))),
            QStringLiteral("Ovale"));

  const int circle_index = type->findData(QStringLiteral("circle"));
  ASSERT_GE(circle_index, 0);
  type->setCurrentIndex(circle_index);
  ASSERT_TRUE(process_events_until([&] {
    return panel.findChild<QDoubleSpinBox *>(QStringLiteral(
               "tool_settings.control.curve.radius")) != nullptr;
  }, 1000));
  type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.curve.type"));
  auto *radius = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.curve.radius"));
  auto *radius_label = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.control.curve.radius.label"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(radius, nullptr);
  ASSERT_NE(radius_label, nullptr);
  EXPECT_EQ(type->currentData().toString(), QStringLiteral("circle"));
  EXPECT_EQ(radius_label->text(), QStringLiteral("Raggio"));
  EXPECT_EQ(panel.findChild<QCheckBox *>(
                QStringLiteral("tool_settings.control.curve.closed")),
            nullptr);
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(
                QStringLiteral("tool_settings.control.curve.length")),
            nullptr);

  const int oval_index = type->findData(QStringLiteral("oval"));
  ASSERT_GE(oval_index, 0);
  type->setCurrentIndex(oval_index);
  ASSERT_TRUE(process_events_until([&] {
    return panel.findChild<QDoubleSpinBox *>(QStringLiteral(
               "tool_settings.control.curve.length")) != nullptr;
  }, 1000));
  type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.curve.type"));
  radius = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.curve.radius"));
  auto *length = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.curve.length"));
  auto *length_label = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.control.curve.length.label"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(radius, nullptr);
  ASSERT_NE(length, nullptr);
  ASSERT_NE(length_label, nullptr);
  EXPECT_EQ(type->currentData().toString(), QStringLiteral("oval"));
  EXPECT_EQ(length_label->text(), QStringLiteral("Lunghezza"));
  EXPECT_DOUBLE_EQ(radius->minimum(), 0.001);
  EXPECT_DOUBLE_EQ(radius->maximum(), 2.0);
  EXPECT_DOUBLE_EQ(radius->singleStep(), 0.001);
  EXPECT_DOUBLE_EQ(length->minimum(), 2.0);
  EXPECT_DOUBLE_EQ(length->maximum(), 200000.0);
  EXPECT_DOUBLE_EQ(length->singleStep(), 0.001);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     PassiveSelectedCurveSolidifyRetainsGenericEnabledEditors) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-passive-curve-solidify-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {3.0F, 0.0F, 0.0F}}},
      .name = "Passive Curve Solidify UI",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  ASSERT_EQ(editor.tools().active_tool(), "select");

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *description_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.description"));
  auto *settings_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.settings"));
  auto *solidify = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.curve.solidify"));
  auto *resolution_row = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.control.curve.solidify.resolution.row"));
  auto *resolution = panel.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.curve.solidify.resolution"));
  auto *radius_row = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.control.curve.solidify.radius.row"));
  auto *radius = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.curve.solidify.radius"));
  auto *apply_solidify = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.control.curve.solidify.apply"));
  ASSERT_NE(description_section, nullptr);
  ASSERT_NE(settings_section, nullptr);
  ASSERT_NE(solidify, nullptr);
  ASSERT_NE(resolution_row, nullptr);
  ASSERT_NE(resolution, nullptr);
  ASSERT_NE(radius_row, nullptr);
  ASSERT_NE(radius, nullptr);
  ASSERT_NE(apply_solidify, nullptr);
  EXPECT_TRUE(panel.has_active_target());
  EXPECT_FALSE(description_section->isHidden());
  EXPECT_FALSE(settings_section->isHidden());
  EXPECT_FALSE(solidify->isChecked());
  EXPECT_FALSE(resolution_row->isEnabled());
  EXPECT_FALSE(resolution->isEnabled());
  EXPECT_FALSE(radius_row->isEnabled());
  EXPECT_FALSE(radius->isEnabled());
  EXPECT_FALSE(apply_solidify->isEnabled());
  EXPECT_EQ(resolution->value(), 8);
  EXPECT_NEAR(radius->value(), 0.1, 1.0e-6);

  const QPointer<QCheckBox> retained_solidify(solidify);
  const QPointer<QWidget> retained_resolution_row(resolution_row);
  const QPointer<QSpinBox> retained_resolution(resolution);
  const QPointer<QWidget> retained_radius_row(radius_row);
  const QPointer<QDoubleSpinBox> retained_radius(radius);
  const QPointer<QPushButton> retained_apply_solidify(apply_solidify);
  solidify->click();
  panel.refresh();
  QCoreApplication::processEvents();
  EXPECT_EQ(panel.findChild<QCheckBox *>(
                QStringLiteral("tool_settings.control.curve.solidify")),
            retained_solidify.data());
  EXPECT_EQ(panel.findChild<QWidget *>(QStringLiteral(
                "tool_settings.control.curve.solidify.resolution.row")),
            retained_resolution_row.data());
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.curve.solidify.resolution")),
            retained_resolution.data());
  EXPECT_EQ(panel.findChild<QWidget *>(QStringLiteral(
                "tool_settings.control.curve.solidify.radius.row")),
            retained_radius_row.data());
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.curve.solidify.radius")),
            retained_radius.data());
  EXPECT_EQ(panel.findChild<QPushButton *>(QStringLiteral(
                "tool_settings.control.curve.solidify.apply")),
            retained_apply_solidify.data());
  EXPECT_TRUE(retained_solidify->isChecked());
  EXPECT_TRUE(retained_resolution_row->isEnabled());
  EXPECT_TRUE(retained_resolution->isEnabled());
  EXPECT_TRUE(retained_radius_row->isEnabled());
  EXPECT_TRUE(retained_radius->isEnabled());
  EXPECT_TRUE(retained_apply_solidify->isEnabled());

  ASSERT_TRUE(editor.tools()
                  .set_setting("curve", "curve.solidify.resolution", 24.0)
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("curve", "curve.solidify.radius", 0.75)
                  .ok());
  panel.refresh();
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.curve.solidify.resolution")),
            retained_resolution.data());
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.curve.solidify.radius")),
            retained_radius.data());
  EXPECT_EQ(retained_resolution->value(), 24);
  EXPECT_DOUBLE_EQ(retained_radius->value(), 0.75);

  ASSERT_TRUE(editor.selection().clear().ok());
  panel.refresh();
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel.has_active_target());
  EXPECT_TRUE(description_section->isHidden());
  EXPECT_TRUE(settings_section->isHidden());
  EXPECT_TRUE(retained_solidify.isNull());
  EXPECT_TRUE(retained_resolution.isNull());
  EXPECT_TRUE(retained_radius.isNull());
  EXPECT_TRUE(retained_apply_solidify.isNull());
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     PassiveCurveSolidifyActionFlushesLatestNumericDraftBeforeDispatch) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-passive-curve-solidify-apply-flush-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {3.0F, 0.0F, 0.0F}}},
      .name = "Pending Solidify Radius",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *solidify = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.curve.solidify"));
  ASSERT_NE(solidify, nullptr);
  solidify->click();
  panel.refresh();

  auto *radius = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.curve.solidify.radius"));
  auto *apply_solidify = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.control.curve.solidify.apply"));
  ASSERT_NE(radius, nullptr);
  ASSERT_NE(apply_solidify, nullptr);
  ASSERT_TRUE(apply_solidify->isEnabled());

  session.history().clear();
  radius->setValue(0.75);
  const auto radius_target = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *owner_radius =
      find_setting(radius_target, "curve.solidify.radius");
  ASSERT_NE(owner_radius, nullptr);
  EXPECT_NEAR(owner_radius->number_value, 0.1, 1.0e-6)
      << "the coalesced edit must still be pending before the action";

  apply_solidify->click();
  const auto source_after_apply =
      editor.query().entity(curve.value().entity);
  ASSERT_TRUE(source_after_apply.has_value());
  ASSERT_TRUE(source_after_apply->has_curve);
  EXPECT_NEAR(source_after_apply->curve_solidify_radius, 0.75F, 1.0e-6F);
  const std::vector<quader::QuaderEntitySnapshot> entities_after_apply =
      editor.query().entities();
  EXPECT_EQ(static_cast<std::size_t>(std::count_if(
                entities_after_apply.begin(), entities_after_apply.end(),
                [](const quader::QuaderEntitySnapshot &entity) {
                  return entity.has_mesh;
                })),
            1U)
      << "Apply Solidify must observe the flushed Radius and create the mesh";

  ASSERT_TRUE(editor.history().undo().ok());
  const std::vector<quader::QuaderEntitySnapshot> entities_after_apply_undo =
      editor.query().entities();
  EXPECT_FALSE(std::any_of(
      entities_after_apply_undo.begin(), entities_after_apply_undo.end(),
      [](const quader::QuaderEntitySnapshot &entity) {
        return entity.has_mesh;
      }));
  const auto source_after_apply_undo =
      editor.query().entity(curve.value().entity);
  ASSERT_TRUE(source_after_apply_undo.has_value());
  EXPECT_NEAR(source_after_apply_undo->curve_solidify_radius, 0.75F, 1.0e-6F)
      << "the numeric edit must precede the separately undoable Apply action";
  ASSERT_TRUE(editor.history().undo().ok());
  const auto source_after_radius_undo =
      editor.query().entity(curve.value().entity);
  ASSERT_TRUE(source_after_radius_undo.has_value());
  EXPECT_NEAR(source_after_radius_undo->curve_solidify_radius, 0.1F, 1.0e-6F);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     PassiveCurveSolidifyNumericGesturesMergeWhileArrowsStayDiscrete) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-passive-curve-solidify-gesture-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto curve = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F}},
                 {.position = {3.0F, 0.0F, 0.0F}}},
      .name = "Passive Curve Solidify Gestures",
  });
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  ASSERT_TRUE(
      editor.selection().set_mode(quader::QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(editor.selection().select_object(curve.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Curve).ok());
  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(360, 640);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *solidify = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.curve.solidify"));
  auto *resolution_spinner =
      dynamic_cast<quader::QtToolSettingsDragSpinner *>(
          panel.findChild<QWidget *>(QStringLiteral(
              "tool_settings.control.curve.solidify.resolution.spinner")));
  auto *resolution_editor = resolution_spinner != nullptr
                                ? resolution_spinner->findChild<QLineEdit *>(
                                      QStringLiteral(
                                          "drag_spinner.editor.line_edit"))
                                : nullptr;
  auto *resolution_increase =
      resolution_spinner != nullptr
          ? resolution_spinner->findChild<QToolButton *>(
                QStringLiteral("drag_spinner.increase"))
          : nullptr;
  auto *radius_slider = panel.findChild<QSlider *>(QStringLiteral(
      "tool_settings.control.curve.solidify.radius.slider"));
  ASSERT_NE(solidify, nullptr);
  ASSERT_NE(resolution_spinner, nullptr);
  ASSERT_NE(resolution_editor, nullptr);
  ASSERT_NE(resolution_increase, nullptr);
  ASSERT_NE(radius_slider, nullptr);
  solidify->click();
  panel.refresh();
  ASSERT_TRUE(resolution_spinner->isEnabled());
  ASSERT_TRUE(radius_slider->isEnabled());

  const auto owner_number = [&editor](std::string_view id) {
    const auto target = editor.tools().active_settings();
    const quader::QuaderToolSettingControl *setting =
        find_setting(target, id);
    EXPECT_NE(setting, nullptr);
    return setting != nullptr ? setting->number_value : 0.0;
  };

  session.history().clear();
  const QPoint resolution_press = resolution_editor->rect().center();
  const QPoint resolution_crossed(
      resolution_press.x() + QApplication::startDragDistance() + 1,
      resolution_press.y());
  const QPoint resolution_first(resolution_crossed.x() + 8,
                                resolution_crossed.y());
  const QPoint resolution_second(resolution_crossed.x() + 24,
                                 resolution_crossed.y());
  send_mouse_event(*resolution_editor, QEvent::MouseButtonPress,
                   resolution_press, Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*resolution_editor, QEvent::MouseMove,
                   resolution_crossed, Qt::NoButton, Qt::LeftButton);
  send_mouse_event(*resolution_editor, QEvent::MouseMove, resolution_first,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(resolution_spinner->is_dragging());
  ASSERT_TRUE(process_events_until([&] {
    return owner_number("curve.solidify.resolution") == 9.0;
  }, 1000));
  EXPECT_DOUBLE_EQ(owner_number("curve.solidify.resolution"), 9.0);
  send_mouse_event(*resolution_editor, QEvent::MouseMove, resolution_second,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(process_events_until([&] {
    return owner_number("curve.solidify.resolution") == 11.0;
  }, 1000));
  EXPECT_DOUBLE_EQ(owner_number("curve.solidify.resolution"), 11.0);
  send_mouse_event(*resolution_editor, QEvent::MouseButtonRelease,
                   resolution_second, Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_EQ(resolution_spinner->value(), 8);
  EXPECT_FALSE(session.history().can_undo())
      << "periodic Resolution previews must merge into one gesture";

  session.history().clear();
  resolution_increase->click();
  resolution_increase->click();
  ASSERT_TRUE(process_events_until([&] {
    return owner_number("curve.solidify.resolution") == 10.0;
  }, 1000));
  EXPECT_DOUBLE_EQ(owner_number("curve.solidify.resolution"), 10.0);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_EQ(resolution_spinner->value(), 9);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_EQ(resolution_spinner->value(), 8)
      << "each arrow activation must remain one discrete history step";

  session.history().clear();
  EXPECT_FALSE(resolution_spinner->spin_box()->keyboardTracking());
  const QPoint resolution_click = resolution_editor->rect().center();
  send_mouse_event(*resolution_editor, QEvent::MouseButtonPress,
                   resolution_click, Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*resolution_editor, QEvent::MouseButtonRelease,
                   resolution_click, Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(resolution_spinner->is_editing());
  ASSERT_TRUE(resolution_editor->hasSelectedText());
  send_text_event(*resolution_editor, u'1');
  send_text_event(*resolution_editor, u'6');
  process_events_for(75);
  EXPECT_DOUBLE_EQ(owner_number("curve.solidify.resolution"), 8.0)
      << "a text gesture must stay local until its explicit commit";
  send_key_event(*resolution_editor, Qt::Key_Return);
  EXPECT_DOUBLE_EQ(owner_number("curve.solidify.resolution"), 16.0);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_EQ(resolution_spinner->value(), 8);
  EXPECT_FALSE(session.history().can_undo());

  session.history().clear();
  const QRect radius_groove = slider_groove(*radius_slider);
  const QRect radius_handle = slider_handle(*radius_slider);
  const double radius_before_drag = owner_number("curve.solidify.radius");
  ASSERT_GT(radius_groove.width(), 20);
  const QPoint radius_press = radius_handle.center();
  const QPoint radius_first(radius_groove.left() + radius_groove.width() / 3,
                            radius_groove.center().y());
  const QPoint radius_second(
      radius_groove.left() + (radius_groove.width() * 2) / 3,
      radius_groove.center().y());
  send_mouse_event(*radius_slider, QEvent::MouseButtonPress, radius_press,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*radius_slider, QEvent::MouseMove, radius_first,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(process_events_until([&] {
    return owner_number("curve.solidify.radius") != radius_before_drag;
  }, 1000));
  const double first_radius = owner_number("curve.solidify.radius");
  EXPECT_NE(first_radius, radius_before_drag);
  send_mouse_event(*radius_slider, QEvent::MouseMove, radius_second,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(process_events_until(
      [&] { return owner_number("curve.solidify.radius") != first_radius; },
      1000));
  const double final_radius = owner_number("curve.solidify.radius");
  EXPECT_NE(final_radius, first_radius);
  send_mouse_event(*radius_slider, QEvent::MouseButtonRelease, radius_second,
                   Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_NEAR(owner_number("curve.solidify.radius"), 0.1, 1.0e-6);
  EXPECT_FALSE(session.history().can_undo())
      << "periodic Radius previews must merge into one gesture";

  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     DockedNativeWindowClassificationAndTabRefreshUseDockSignals) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-native-window-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);

  QMainWindow main_window;
  main_window.setAttribute(Qt::WA_DontShowOnScreen, true);
  main_window.resize(900, 600);
  auto *central = new QWidget(&main_window);
  main_window.setCentralWidget(central);

  auto *tool_settings_dock = new QDockWidget(&main_window);
  auto *tool_settings = new quader::QtToolSettingsPanel();
  tool_settings_dock->setWidget(tool_settings);
  main_window.addDockWidget(Qt::RightDockWidgetArea, tool_settings_dock);
  auto *inspector_dock = new QDockWidget(&main_window);
  inspector_dock->setWidget(new QWidget());
  main_window.addDockWidget(Qt::RightDockWidgetArea, inspector_dock);
  quader::UIPanelManager panels;
  panels.add(quader::UIPanelId::ToolSettings, *tool_settings_dock);
  panels.add(quader::UIPanelId::Inspector, *inspector_dock);
  main_window.tabifyDockWidget(inspector_dock, tool_settings_dock);

  main_window.show();
  tool_settings_dock->show();
  tool_settings_dock->raise();
  QCoreApplication::processEvents();
  QWindow *native_main_window = main_window.windowHandle();
  ASSERT_NE(native_main_window, nullptr);
  const QRegion tool_settings_region = tool_settings->visibleRegion();
  ASSERT_FALSE(tool_settings_region.isEmpty());
  const QPoint tool_settings_point =
      tool_settings->mapToGlobal(tool_settings_region.begin()->center());
  const QPoint viewport_point = central->mapToGlobal(central->rect().center());

  // The shared main QWindow is not blanket-classified as Tool Settings.
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(
      native_main_window));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(
      native_main_window, tool_settings_point));
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(
      native_main_window, viewport_point));
  EXPECT_TRUE(panels.is_presented(quader::UIPanelId::ToolSettings));
  EXPECT_TRUE(
      panels.set_visible(quader::UIPanelId::ToolSettings, false));
  QCoreApplication::processEvents();
  EXPECT_FALSE(panels.is_presented(quader::UIPanelId::ToolSettings));
  EXPECT_TRUE(panels.set_visible(quader::UIPanelId::ToolSettings, true));
  tool_settings_dock->raise();
  QCoreApplication::processEvents();
  EXPECT_TRUE(panels.is_presented(quader::UIPanelId::ToolSettings));

  inspector_dock->raise();
  QCoreApplication::processEvents();
  EXPECT_FALSE(panels.is_presented(quader::UIPanelId::ToolSettings));
  EXPECT_TRUE(panels.is_presented(quader::UIPanelId::Inspector));
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(
      native_main_window, tool_settings_point));
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
  quader::QtToolSettingsPanel window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
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

  // The same focused-settings fallback is owned by the native lifecycle,
  // rather than by Primitive-specific Qt policy.
  active_tool = quader::ViewportToolId::LinearArray;
  ASSERT_TRUE(editor.tools().activate(active_tool).ok());
  window.refresh();
  QCoreApplication::processEvents();
  auto *linear_count = window.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.linear_array.count"));
  ASSERT_NE(linear_count, nullptr);
  QLineEdit *linear_count_editor = linear_count->findChild<QLineEdit *>();
  ASSERT_NE(linear_count_editor, nullptr);
  linear_count_editor->setText(QStringLiteral("3"));
  linear_count_editor->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_event(*linear_count_editor, Qt::Key_Space);

  EXPECT_EQ(linear_count->value(), 3);
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_TRUE(editor.history().can_undo());

  application.removeEventFilter(&command_filter);
  window.unbind();
  window.hide();
}

TEST(QtToolSettingsNumberSliderTests,
     PrimitiveSubdivisionPointerDragPublishesBoundedLiveGeometry) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-primitive-slider-preview-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(tool_settings_primitive_preview(),
                                  "Live Slider Box")
                  .ok);

  const std::vector<quader::ModelingObjectRecord> objects =
      session.modeling().objects();
  ASSERT_EQ(objects.size(), 1u);
  const quader::ModelingObjectRecord &live_box = objects.front();
  const std::optional<quader::QuaderEntitySnapshot> entity_before =
      editor.query().entity(live_box.entity);
  ASSERT_TRUE(entity_before.has_value());
  ASSERT_TRUE(entity_before->has_mesh);
  ASSERT_EQ(entity_before->mesh, live_box.mesh);
  const std::optional<quader::QuaderMeshSnapshot> mesh_before =
      editor.query().mesh(live_box.mesh);
  ASSERT_TRUE(mesh_before.has_value());
  ASSERT_EQ(mesh_before->index_count, 36u);

  quader::QtToolSettingsPanel window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.bind(editor);
  window.show();
  window.activateWindow();
  QCoreApplication::processEvents();

  auto *subdivisions_slider = window.findChild<QSlider *>(QStringLiteral(
      "tool_settings.control.primitive.box.subdivisions_x.slider"));
  auto *subdivisions_spin = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.box.subdivisions_x"));
  ASSERT_NE(subdivisions_slider, nullptr);
  ASSERT_NE(subdivisions_spin, nullptr);
  const QRect groove = slider_groove(*subdivisions_slider);
  const QPoint press = slider_handle(*subdivisions_slider).center();
  ASSERT_FALSE(groove.isEmpty());
  ASSERT_FALSE(slider_handle(*subdivisions_slider).isEmpty());
  const int delta = std::max(4, groove.width() / 16);
  const std::array<QPoint, 5> moves{
      QPoint(std::min(groove.right(), press.x() + delta), groove.center().y()),
      QPoint(std::min(groove.right(), press.x() + delta * 2),
             groove.center().y()),
      QPoint(std::min(groove.right(), press.x() + delta * 3),
             groove.center().y()),
      QPoint(std::min(groove.right(), press.x() + delta * 4),
             groove.center().y()),
      QPoint(std::min(groove.right(), press.x() + delta * 5),
             groove.center().y()),
  };
  ASSERT_NE(moves.front().x(), press.x());
  ASSERT_LT(moves.front().x(), moves.back().x());

  const quader::QuaderRevisionStamp revisions_before = editor.revisions();
  const std::uint64_t settings_revision_before =
      editor.tools().active_settings_revision();
  const quader::ModelingSdkProjectionStats projection_before =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  send_mouse_event(*subdivisions_slider, QEvent::MouseButtonPress, press,
                   Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(subdivisions_slider->isSliderDown());
  for (std::size_t index = 0; index < 3; ++index) {
    send_mouse_event(*subdivisions_slider, QEvent::MouseMove, moves[index],
                     Qt::NoButton, Qt::LeftButton);
  }
  const int first_pending_subdivisions = subdivisions_spin->value();
  ASSERT_GT(first_pending_subdivisions, 1);
  EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x, 1);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_before);
  const std::optional<quader::QuaderMeshSnapshot> mesh_before_live_tick =
      editor.query().mesh(live_box.mesh);
  ASSERT_TRUE(mesh_before_live_tick.has_value());
  EXPECT_EQ(mesh_before_live_tick->revision, mesh_before->revision);
  EXPECT_EQ(mesh_before_live_tick->index_count, mesh_before->index_count);

  // Rapid pointer motion publishes one latest-value primitive rebuild at the
  // fixed preview cadence while the slider remains held.
  ASSERT_TRUE(process_events_until([&] {
    return session.primitive_creation().box_settings().subdivisions_x ==
           first_pending_subdivisions;
  }, 1000));
  ASSERT_TRUE(subdivisions_slider->isSliderDown());
  EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x,
            first_pending_subdivisions);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_before + 1u);
  const quader::ModelingSdkProjectionStats projection_after_live_tick =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection_after_live_tick.mesh_compile_count,
            projection_before.mesh_compile_count + 1u);
  EXPECT_EQ(projection_after_live_tick.asset_publication_count,
            projection_before.asset_publication_count + 1u);
  EXPECT_EQ(projection_after_live_tick.scene_publication_count,
            projection_before.scene_publication_count + 1u);
  const quader::QuaderRevisionStamp revisions_after_live_tick =
      editor.revisions();
  EXPECT_GT(revisions_after_live_tick.asset_revision,
            revisions_before.asset_revision);
  EXPECT_GT(revisions_after_live_tick.modeling_content_revision,
            revisions_before.modeling_content_revision);
  const std::optional<quader::QuaderEntitySnapshot> entity_after_live_tick =
      editor.query().entity(live_box.entity);
  ASSERT_TRUE(entity_after_live_tick.has_value());
  EXPECT_EQ(entity_after_live_tick->mesh, live_box.mesh);
  const std::optional<quader::QuaderMeshSnapshot> mesh_after_live_tick =
      editor.query().mesh(live_box.mesh);
  ASSERT_TRUE(mesh_after_live_tick.has_value());
  EXPECT_GT(mesh_after_live_tick->revision, mesh_before->revision);
  EXPECT_GT(mesh_after_live_tick->index_count, mesh_before->index_count);

  const auto live_target = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *live_setting =
      find_setting(live_target, "primitive.box.subdivisions_x");
  ASSERT_NE(live_setting, nullptr);
  EXPECT_DOUBLE_EQ(live_setting->number_value,
                   static_cast<double>(first_pending_subdivisions));

  // An idle held slider performs no further owner, projection, or geometry
  // work until another value arrives.
  process_events_for(75);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_before + 1u);
  const quader::ModelingSdkProjectionStats projection_after_idle =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection_after_idle.mesh_compile_count,
            projection_after_live_tick.mesh_compile_count);
  EXPECT_EQ(projection_after_idle.asset_publication_count,
            projection_after_live_tick.asset_publication_count);
  EXPECT_EQ(projection_after_idle.scene_publication_count,
            projection_after_live_tick.scene_publication_count);
  const std::optional<quader::QuaderMeshSnapshot> mesh_after_idle =
      editor.query().mesh(live_box.mesh);
  ASSERT_TRUE(mesh_after_idle.has_value());
  EXPECT_EQ(mesh_after_idle->revision, mesh_after_live_tick->revision);
  EXPECT_EQ(mesh_after_idle->index_count, mesh_after_live_tick->index_count);

  // Release before the next cadence tick: it must publish the exact final
  // slider value once, then leave no delayed publication behind.
  for (std::size_t index = 3; index < moves.size(); ++index) {
    send_mouse_event(*subdivisions_slider, QEvent::MouseMove, moves[index],
                     Qt::NoButton, Qt::LeftButton);
  }
  const int final_pending_subdivisions = subdivisions_spin->value();
  ASSERT_GT(final_pending_subdivisions, first_pending_subdivisions);
  EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x,
            first_pending_subdivisions);
  send_mouse_event(*subdivisions_slider, QEvent::MouseButtonRelease,
                   moves.back(), Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(subdivisions_slider->isSliderDown());
  EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x,
            final_pending_subdivisions);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_before + 2u);
  const quader::ModelingSdkProjectionStats projection_after_release =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection_after_release.mesh_compile_count,
            projection_before.mesh_compile_count + 2u);
  EXPECT_EQ(projection_after_release.asset_publication_count,
            projection_before.asset_publication_count + 2u);
  EXPECT_EQ(projection_after_release.scene_publication_count,
            projection_before.scene_publication_count + 2u);
  const std::optional<quader::QuaderMeshSnapshot> mesh_after_release =
      editor.query().mesh(live_box.mesh);
  ASSERT_TRUE(mesh_after_release.has_value());
  EXPECT_GT(mesh_after_release->revision, mesh_after_live_tick->revision);
  EXPECT_GT(mesh_after_release->index_count,
            mesh_after_live_tick->index_count);

  process_events_for(75);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_before + 2u);
  const quader::ModelingSdkProjectionStats projection_after_release_idle =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(projection_after_release_idle.mesh_compile_count,
            projection_after_release.mesh_compile_count);
  EXPECT_EQ(projection_after_release_idle.asset_publication_count,
            projection_after_release.asset_publication_count);
  EXPECT_EQ(projection_after_release_idle.scene_publication_count,
            projection_after_release.scene_publication_count);
  const std::optional<quader::QuaderMeshSnapshot>
      mesh_after_release_idle = editor.query().mesh(live_box.mesh);
  ASSERT_TRUE(mesh_after_release_idle.has_value());
  EXPECT_EQ(mesh_after_release_idle->revision, mesh_after_release->revision);
  EXPECT_EQ(mesh_after_release_idle->index_count,
            mesh_after_release->index_count);

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

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
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

  quader::QtToolSettingsPanel window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.bind(editor);
  window.show();
  window.activateWindow();
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
  EXPECT_TRUE(window.numeric_interaction_active());
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
  EXPECT_TRUE(window.numeric_interaction_active());
  EXPECT_DOUBLE_EQ(width_spin->value(), pending_width);
  const auto width_release_start = std::chrono::steady_clock::now();
  send_mouse_event(*width_slider, QEvent::MouseButtonRelease, width_move,
                   Qt::LeftButton, Qt::NoButton);
  const auto width_release_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                width_release_start);
  EXPECT_FALSE(width_slider->isSliderDown());
  EXPECT_FALSE(window.numeric_interaction_active());
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
  const std::array<QPoint, 5> segments_moves{
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta),
             segments_groove.center().y()),
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta * 2),
             segments_groove.center().y()),
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta * 3),
             segments_groove.center().y()),
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta * 4),
             segments_groove.center().y()),
      QPoint(std::min(segments_groove.right(),
                      segments_press.x() + segments_delta * 5),
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
  const std::uint64_t segments_settings_revision_before =
      editor.tools().active_settings_revision();
  const quader::ModelingSdkProjectionStats segments_projection_before =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  const auto segments_start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < 3; ++index) {
    send_mouse_event(*segments_slider, QEvent::MouseMove,
                     segments_moves[index],
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
  EXPECT_TRUE(window.numeric_interaction_active());

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
  EXPECT_TRUE(window.numeric_interaction_active());
  EXPECT_EQ(segments_spin->value(), pending_segments);
  ASSERT_TRUE(process_events_until([&] {
    return editor.revisions().asset_revision > segments_revisions_before.asset_revision;
  }, 1000));
  const quader::QuaderRevisionStamp segments_revisions_after_live_update =
      editor.revisions();
  EXPECT_GT(segments_revisions_after_live_update.asset_revision,
            segments_revisions_before.asset_revision);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            segments_settings_revision_before + 1u);
  EXPECT_EQ(session.modeling()
                .modeling_sdk_projection_stats_for_tests()
                .preview_publication_count,
            segments_projection_before.preview_publication_count + 1u);
  const quader::QuaderToolSettingsTarget live_segments_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *live_segments_setting =
      find_setting(live_segments_settings,
                   "operation.edge_bevel.segments");
  ASSERT_NE(live_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(live_segments_setting->number_value,
                   static_cast<double>(pending_segments));
  EXPECT_TRUE(window.numeric_interaction_active());

  // Holding the pointer idle after the fixed live-preview tick must not
  // continuously rebuild the same topology.
  process_events_for(75);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_after_live_update.asset_revision);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            segments_settings_revision_before + 1u);
  EXPECT_EQ(session.modeling()
                .modeling_sdk_projection_stats_for_tests()
                .preview_publication_count,
            segments_projection_before.preview_publication_count + 1u);

  // Continued movement starts the next fixed window. Releasing before it
  // expires flushes the exact latest value once and cancels the timer.
  for (std::size_t index = 3; index < segments_moves.size(); ++index) {
    send_mouse_event(*segments_slider, QEvent::MouseMove,
                     segments_moves[index], Qt::NoButton, Qt::LeftButton);
  }
  EXPECT_EQ(segments_slider_moves, 5);
  EXPECT_EQ(segments_slider_changes, 5);
  EXPECT_EQ(segments_spin_changes, 0);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_after_live_update.asset_revision);
  EXPECT_TRUE(window.numeric_interaction_active());
  const int final_pending_segments = segments_spin->value();
  EXPECT_NE(final_pending_segments, pending_segments);
  window.refresh();
  EXPECT_EQ(segments_slider_destroys, 0);
  EXPECT_EQ(segments_spin->value(), final_pending_segments);
  send_mouse_event(*segments_slider, QEvent::MouseButtonRelease,
                   segments_moves.back(), Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(segments_slider->isSliderDown());
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_EQ(segments_slider_changes, 5);
  EXPECT_EQ(segments_spin_changes, 0);
  EXPECT_GT(editor.revisions().asset_revision,
            segments_revisions_after_live_update.asset_revision);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            segments_settings_revision_before + 2u);
  EXPECT_EQ(session.modeling()
                .modeling_sdk_projection_stats_for_tests()
                .preview_publication_count,
            segments_projection_before.preview_publication_count + 2u);
  const quader::QuaderToolSettingsTarget applied_segments_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *applied_segments_setting =
      find_setting(applied_segments_settings,
                   "operation.edge_bevel.segments");
  ASSERT_NE(applied_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(applied_segments_setting->number_value,
                   static_cast<double>(final_pending_segments));
  const quader::QuaderRevisionStamp segments_revisions_after_release =
      editor.revisions();
  process_events_for(75);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_after_release.asset_revision);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            segments_settings_revision_before + 2u);
  EXPECT_EQ(session.modeling()
                .modeling_sdk_projection_stats_for_tests()
                .preview_publication_count,
            segments_projection_before.preview_publication_count + 2u);

  // Spin arrows, wheel, and keyboard input all arrive through valueChanged.
  // Several synchronous changes stay presentation-local until editing ends.
  const int spin_value_before = segments_spin->value();
  const int forward_capacity = segments_spin->maximum() - spin_value_before;
  const int backward_capacity = spin_value_before - segments_spin->minimum();
  ASSERT_TRUE(forward_capacity >= 3 || backward_capacity >= 3);
  const int spin_direction = forward_capacity >= 3 ? 1 : -1;
  const std::array<int, 3> spin_values{
      spin_value_before + spin_direction,
      spin_value_before + spin_direction * 2,
      spin_value_before + spin_direction * 3,
  };
  const quader::QuaderRevisionStamp spin_revisions_before = editor.revisions();
  const int spin_changes_before = segments_spin_changes;
  for (const int value : spin_values) {
    segments_spin->setValue(value);
  }
  EXPECT_EQ(segments_spin_changes - spin_changes_before, 3);
  EXPECT_EQ(editor.revisions().asset_revision,
            spin_revisions_before.asset_revision);
  EXPECT_TRUE(window.numeric_interaction_active());
  window.refresh();
  EXPECT_EQ(segments_slider_destroys, 0);
  EXPECT_EQ(segments_spin->value(), spin_values.back());
  ASSERT_TRUE(QMetaObject::invokeMethod(segments_spin, "editingFinished",
                                        Qt::DirectConnection));
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_GT(editor.revisions().asset_revision,
            spin_revisions_before.asset_revision);
  const auto spin_applied_target = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *spin_applied_setting =
      find_setting(spin_applied_target, "operation.edge_bevel.segments");
  ASSERT_NE(spin_applied_setting, nullptr);
  EXPECT_DOUBLE_EQ(spin_applied_setting->number_value,
                   static_cast<double>(spin_values.back()));
  const quader::QuaderRevisionStamp spin_revisions_after_flush =
      editor.revisions();
  process_events_for(75);
  EXPECT_EQ(editor.revisions().asset_revision,
            spin_revisions_after_flush.asset_revision);

  // A real mouse Reset begins while the spin editor still owns focus. The
  // press invalidates its delayed value before the click transfers focus and
  // emits editingFinished, so Reset remains the only owner mutation.
  const int stale_reset_value =
      spin_values.back() == segments_spin->maximum()
          ? segments_spin->minimum()
          : segments_spin->maximum();
  segments_spin->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_TRUE(segments_spin->hasFocus() ||
              (QApplication::focusWidget() != nullptr &&
               segments_spin->isAncestorOf(QApplication::focusWidget())));
  segments_spin->setValue(stale_reset_value);
  EXPECT_TRUE(window.numeric_interaction_active());
  auto *reset_button = window.findChild<QPushButton *>(
      QStringLiteral("tool_settings.reset_defaults"));
  ASSERT_NE(reset_button, nullptr);
  send_mouse_event(*reset_button, QEvent::MouseButtonPress,
                   reset_button->rect().center(), Qt::LeftButton,
                   Qt::LeftButton);
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_TRUE(segments_spin->hasFocus() ||
              (QApplication::focusWidget() != nullptr &&
               segments_spin->isAncestorOf(QApplication::focusWidget())));
  send_mouse_event(*reset_button, QEvent::MouseButtonRelease,
                   reset_button->rect().center(), Qt::LeftButton,
                   Qt::NoButton);
  EXPECT_TRUE(reset_button->hasFocus());
  QCoreApplication::processEvents();
  EXPECT_FALSE(window.numeric_interaction_active());
  auto reset_segments_target = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *reset_segments_setting =
      find_setting(reset_segments_target,
                   "operation.edge_bevel.segments");
  ASSERT_NE(reset_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(reset_segments_setting->number_value, 1.0);
  process_events_for(75);
  reset_segments_target = editor.tools().active_settings();
  reset_segments_setting = find_setting(reset_segments_target,
                                        "operation.edge_bevel.segments");
  ASSERT_NE(reset_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(reset_segments_setting->number_value, 1.0);

  // Ctrl+Z semantics cancel a live preview before touching document history.
  // A value queued against that preview epoch must disappear rather than
  // mutating the persistent interaction setting after the timer wakes.
  segments_spin->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  const int stale_undo_value = stale_reset_value == 1 ? 2 : stale_reset_value;
  segments_spin->setValue(stale_undo_value);
  ASSERT_TRUE(window.numeric_interaction_active());
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(session.modeling().interaction_settings().edge_bevel_segments, 1);
  const std::uint64_t settings_revision_after_undo =
      editor.tools().active_settings_revision();
  const quader::QuaderRevisionStamp revisions_after_undo = editor.revisions();
  process_events_for(75);
  // Rejection runs on the dispatch timer and schedules a separate refresh.
  // Settle both before opening another preview with the retained spin editor.
  ASSERT_TRUE(process_events_until([&] {
    if (window.numeric_interaction_active()) {
      return false;
    }
    const auto *settled_segments = window.findChild<QSpinBox *>(QStringLiteral(
        "tool_settings.control.operation.edge_bevel.segments"));
    return !window.has_active_target() ||
           (settled_segments != nullptr && settled_segments->value() == 1);
  }, 1000));
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_EQ(session.modeling().interaction_settings().edge_bevel_segments, 1);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_after_undo);
  EXPECT_EQ(editor.revisions().asset_revision,
            revisions_after_undo.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            revisions_after_undo.modeling_content_revision);

  // Re-enter the preview and exercise Cancel through the same real
  // press/release ordering. The focused edit is discarded on press, before
  // focus-out can flush it, and never persists after cancellation.
  ASSERT_TRUE(editor.commands()
                  .execute(editor.commands().command_id_for_modeling_action(
                      "bevel_edges"))
                  .ok());
  window.refresh();
  QCoreApplication::processEvents();
  segments_spin = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.segments"));
  auto *cancel_button = window.findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(segments_spin, nullptr);
  ASSERT_NE(cancel_button, nullptr);
  segments_spin->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  ASSERT_TRUE(segments_spin->hasFocus() ||
              (QApplication::focusWidget() != nullptr &&
               segments_spin->isAncestorOf(QApplication::focusWidget())));
  segments_spin->setValue(stale_undo_value);
  ASSERT_TRUE(window.numeric_interaction_active());
  send_mouse_event(*cancel_button, QEvent::MouseButtonPress,
                   cancel_button->rect().center(), Qt::LeftButton,
                   Qt::LeftButton);
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_EQ(session.modeling().interaction_settings().edge_bevel_segments, 1);
  send_mouse_event(*cancel_button, QEvent::MouseButtonRelease,
                   cancel_button->rect().center(), Qt::LeftButton,
                   Qt::NoButton);
  EXPECT_TRUE(cancel_button->hasFocus());
  QCoreApplication::processEvents();
  const std::uint64_t settings_revision_after_cancel =
      editor.tools().active_settings_revision();
  const quader::QuaderRevisionStamp revisions_after_cancel =
      editor.revisions();
  process_events_for(75);
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_EQ(session.modeling().interaction_settings().edge_bevel_segments, 1);
  EXPECT_EQ(editor.tools().active_settings_revision(),
            settings_revision_after_cancel);
  EXPECT_EQ(editor.revisions().asset_revision,
            revisions_after_cancel.asset_revision);
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            revisions_after_cancel.modeling_content_revision);

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
  RecordProperty("segments_owner_dispatch_batches", 2);
  RecordProperty("segments_spin_coalesced_callbacks",
                 segments_spin_changes - spin_changes_before);
  RecordProperty("segments_three_pointer_moves_us",
                 static_cast<int>(segments_elapsed.count()));

  window.unbind();
  window.hide();
}
