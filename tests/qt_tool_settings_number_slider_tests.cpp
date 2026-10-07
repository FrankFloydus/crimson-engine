////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "core/localization_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "quader/modeling/neutral/polygon/document_model.hpp"
#include "render/scene_render_extraction.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_command_event_filter.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_numeric_spin_box.hpp"
#include "ui/qt_spin_box.hpp"
#include "ui/qt_tool_settings_drag_spinner.hpp"
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_face_texture_controls.hpp"
#include "ui/qt_status_message.hpp"
#include "ui/qt_ui_scale_metrics.hpp"
#include "ui/qt_viewport_panel.hpp"

#include <QApplication>
#include <QAbstractEventDispatcher>
#include <QAbstractItemView>
#include <QButtonGroup>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QDir>
#include <QEventLoop>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
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
#include <QScrollBar>
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
#include <tuple>
#include <utility>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace {

void send_mouse_event(QWidget &target, QEvent::Type type, QPoint position,
                      Qt::MouseButton button, Qt::MouseButtons buttons,
                      Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const QPoint global = target.mapToGlobal(position);
  QMouseEvent event(type, QPointF(position), QPointF(global), button, buttons,
                    modifiers);
  QCoreApplication::sendEvent(&target, &event);
}

QPoint begin_numeric_scrub(
    QLineEdit &editor, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  const QPoint press = editor.rect().center();
  send_mouse_event(editor, QEvent::MouseButtonPress, press, Qt::LeftButton,
                   Qt::LeftButton, modifiers);
  const QPoint crossed =
      press + QPoint(QApplication::startDragDistance() + 1, 0);
  send_mouse_event(editor, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton, modifiers);
  return crossed;
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

TEST(QtFaceTextureControlsTests,
     AppliesInterfaceScaleToStaticTextureControlMetrics) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtFaceTextureControls panel;
  auto *texture_scale = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  ASSERT_NE(texture_scale, nullptr);
  ASSERT_NE(panel.layout(), nullptr);

  panel.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  // The panel is dock content: its minimum must not force a restored or
  // user-resized dock wider when interface scale changes.
  EXPECT_EQ(panel.minimumWidth(), 240);
  EXPECT_EQ(texture_scale->minimumWidth(), 128);
  EXPECT_EQ(panel.layout()->contentsMargins(), QMargins(8, 0, 8, 0));
  const auto *content = panel.findChild<QWidget *>(
      QStringLiteral("face_texture.section.texture.content"));
  ASSERT_NE(content, nullptr);
  ASSERT_NE(content->layout(), nullptr);
  EXPECT_EQ(content->layout()->contentsMargins(), QMargins(12, 12, 12, 12));

  panel.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(panel.minimumWidth(), 240);
  EXPECT_EQ(texture_scale->minimumWidth(), 64);
  EXPECT_EQ(panel.layout()->contentsMargins(), QMargins(4, 0, 4, 0));
  EXPECT_EQ(content->layout()->contentsMargins(), QMargins(6, 6, 6, 6));
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

/// Establishes an Edit context with no selected elements for settings-only tests.
void prepare_empty_component_context(quader::QuaderEditorApi &editor) {
  const auto target = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Component settings target");
  ASSERT_TRUE(target.ok()) << target.error_message();
  ASSERT_TRUE(editor.selection().select_object(target.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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
     SmallRangeDragUsesSixteenPixelsPerUnitRegardlessOfArrowStep) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-scrub-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(2, 20);
  spinner.set_single_step(2);
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

  const QPoint fifteen_pixels_after_origin(crossed.x() + 15, crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, fifteen_pixels_after_origin,
                   Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 10);
  const QPoint sixteen_pixels_after_origin(crossed.x() + 16, crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, sixteen_pixels_after_origin,
                   Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 11);
  const QPoint forty_eight_pixels_after_origin(crossed.x() + 48,
                                               crossed.y());
  send_mouse_event(*editor, QEvent::MouseMove, forty_eight_pixels_after_origin,
                   Qt::NoButton, Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 13)
      << "small integer ranges scrub one unit per sixteen pixels, ignoring arrow step";

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
     ExternalValueAndRangeChangesRebaseTheSharedDrag) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-rebase-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(2, 20);
  spinner.set_value(10);
  spinner.setAttribute(Qt::WA_DontShowOnScreen, true);
  spinner.show();
  QCoreApplication::processEvents();
  auto *editor = spinner.findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(editor, nullptr);
  const QPoint press = editor->rect().center();
  const QPoint crossed(press.x() + QApplication::startDragDistance() + 1,
                       press.y());
  const QPoint two_steps(crossed.x() + 32, crossed.y());
  send_mouse_event(*editor, QEvent::MouseButtonPress, press, Qt::LeftButton,
                   Qt::LeftButton);
  send_mouse_event(*editor, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton);
  send_mouse_event(*editor, QEvent::MouseMove, two_steps, Qt::NoButton,
                   Qt::LeftButton);
  ASSERT_TRUE(spinner.is_dragging());
  ASSERT_EQ(spinner.value(), 12);

  spinner.set_range(8, 11);
  EXPECT_EQ(spinner.value(), 11);
  send_mouse_event(*editor, QEvent::MouseMove, two_steps, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 11);
  const QPoint reverse_one_step(two_steps.x() - 16, two_steps.y());
  send_mouse_event(*editor, QEvent::MouseMove, reverse_one_step, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 10);
  send_key_event(*editor, Qt::Key_Escape);
  EXPECT_FALSE(spinner.is_dragging());
  EXPECT_EQ(spinner.value(), 11)
      << "cancel must retain the value established by the replacement range";
  send_mouse_event(*editor, QEvent::MouseButtonRelease, reverse_one_step,
                   Qt::LeftButton, Qt::NoButton);

  send_mouse_event(*editor, QEvent::MouseButtonPress, press, Qt::LeftButton,
                   Qt::LeftButton);
  send_mouse_event(*editor, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton);
  ASSERT_TRUE(spinner.is_dragging());
  spinner.set_value(8);
  send_mouse_event(*editor, QEvent::MouseMove, crossed, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 8);
  send_mouse_event(*editor, QEvent::MouseMove, reverse_one_step, Qt::NoButton,
                   Qt::LeftButton);
  EXPECT_EQ(spinner.value(), 9);
  send_key_event(*editor, Qt::Key_Escape);
  EXPECT_FALSE(spinner.is_dragging());
  EXPECT_EQ(spinner.value(), 8)
      << "cancel must restore the owner's replacement, not the drag's old value";
  send_mouse_event(*editor, QEvent::MouseButtonRelease, reverse_one_step,
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(spinner.value(), 8);
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
     SharedHoverArrowsApplyExactlyOneStepAndDisableAtTheirBounds) {
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
      QStringLiteral("numeric_spin_box.decrease"));
  QToolButton *right = spinner.findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(left, nullptr);
  ASSERT_NE(right, nullptr);
  EXPECT_EQ(left->parentWidget(), spinner.spin_box());
  EXPECT_EQ(right->parentWidget(), spinner.spin_box());
  QEvent leave(QEvent::Leave);
  QCoreApplication::sendEvent(spinner.spin_box(), &leave);
  EXPECT_FALSE(left->isVisible());
  EXPECT_FALSE(right->isVisible());
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
     LiveInterfaceScalePreservesCompactFiveDigitSizingWithoutDrift) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-drag-spinner-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::QtUiScaleApplicationController scale_controller(application);

  quader::QtToolSettingsDragSpinner spinner;
  spinner.set_range(0, 99'999);
  spinner.set_value(99'999);
  spinner.setAttribute(Qt::WA_DontShowOnScreen, true);
  spinner.show();
  QCoreApplication::processEvents();
  auto *editor = spinner.findChild<QLineEdit *>(
      QStringLiteral("drag_spinner.editor.line_edit"));
  ASSERT_NE(editor, nullptr);
  const auto check_five_digit_width = [&]() {
    spinner.resize(spinner.sizeHint());
    spinner.layout()->activate();
    EXPECT_GE(editor->contentsRect().width(),
              editor->fontMetrics().horizontalAdvance(QStringLiteral("99999")));
  };
  const QSize baseline_size = spinner.sizeHint();
  check_five_digit_width();

  ASSERT_TRUE(scale_controller.apply_factor(2.0F));
  QCoreApplication::processEvents();
  EXPECT_GT(spinner.sizeHint().width(), baseline_size.width());
  EXPECT_GT(spinner.sizeHint().height(), baseline_size.height());
  check_five_digit_width();

  ASSERT_TRUE(scale_controller.apply_factor(1.0F));
  QCoreApplication::processEvents();
  EXPECT_EQ(spinner.sizeHint(), baseline_size);
  check_five_digit_width();
  spinner.hide();
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
  panel.set_expanded(true);
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
      QStringLiteral("numeric_spin_box.decrease"));
  auto *increase = spinner->findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(center, nullptr);
  ASSERT_NE(line_edit, nullptr);
  ASSERT_NE(decrease, nullptr);
  ASSERT_NE(increase, nullptr);
  EXPECT_EQ(spinner->spin_box(), center);
  EXPECT_EQ(panel.findChild<QSlider *>(QStringLiteral(
                "tool_settings.control.path_array.count.slider")),
            nullptr);
  EXPECT_TRUE(panel.findChildren<QSlider *>().isEmpty());
  EXPECT_NE(dynamic_cast<quader::QtDoubleSpinBox *>(
                panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                    "tool_settings.control.path_array.distance"))),
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
  const QPoint three_steps(crossed.x() + 6, crossed.y());
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

  // A scrub can publish intermediate geometry before release. Escape must
  // send the starting value back through the same coalesced owner gesture.
  const QPoint cancel_press = line_edit->rect().center();
  const QPoint cancel_crossed(
      cancel_press.x() + QApplication::startDragDistance() + 1,
      cancel_press.y());
  const QPoint cancel_two_steps(cancel_crossed.x() + 4, cancel_crossed.y());
  send_mouse_event(*line_edit, QEvent::MouseButtonPress, cancel_press,
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseMove, cancel_crossed,
                   Qt::NoButton, Qt::LeftButton);
  send_mouse_event(*line_edit, QEvent::MouseMove, cancel_two_steps,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(spinner->is_dragging());
  ASSERT_TRUE(process_events_until([&] { return owner_count() == 10; }, 1000));
  send_key_event(*line_edit, Qt::Key_Escape);
  EXPECT_FALSE(spinner->is_dragging());
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(spinner->value(), 8);
  EXPECT_EQ(owner_count(), 8);
  process_events_for(75);
  EXPECT_EQ(owner_count(), 8);

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
  panel.set_expanded(true);
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
  const QPoint one_step(crossed.x() + 2, crossed.y());
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
  panel.set_expanded(true);
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

TEST(QtToolSettingsPanelTests,
     ExtrudeCtrlScrubConvertsTheLiveWorldGridToDisplayedSceneUnits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-extrude-numeric-grid-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto box = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Extrude Numeric Grid Source");
  ASSERT_TRUE(box.ok()) << box.error_message();
  ASSERT_TRUE(editor.selection().select_object(box.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(box.value(), 1).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Extrude).ok());

  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *amount = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.extrude.amount"));
  ASSERT_NE(amount, nullptr);
  auto *line_edit = amount->findChild<QLineEdit *>();
  ASSERT_NE(line_edit, nullptr);
  double world_grid = 1.0;
  quader::qt_set_numeric_snap_increment_provider(
      application, panel, [&world_grid]() { return world_grid; });
  const auto send_ctrl_mouse = [&](QEvent::Type type, QPoint position,
                                   Qt::MouseButton button,
                                   Qt::MouseButtons buttons) {
    QMouseEvent event(type, QPointF(position),
                      QPointF(line_edit->mapToGlobal(position)), button,
                      buttons, Qt::ControlModifier);
    QCoreApplication::sendEvent(line_edit, &event);
  };

  for (const auto [grid, displayed_amount] :
       std::array{std::pair{1.0, 32.0}, std::pair{0.5, 16.0}}) {
    SCOPED_TRACE(grid);
    ASSERT_TRUE(editor.tools()
                    .set_setting("extrude", "extrude.amount", 0.0)
                    .ok());
    panel.refresh();
    ASSERT_DOUBLE_EQ(amount->value(), 0.0);
    // The provider is live: changing the scene grid must affect an already
    // constructed Amount editor, whose displayed units are 32 per world unit.
    world_grid = grid;
    const QPoint press = line_edit->rect().center();
    const QPoint crossed(press.x() + QApplication::startDragDistance() + 1,
                         press.y());
    const QPoint moved(crossed.x() + 170, crossed.y());
    send_ctrl_mouse(QEvent::MouseButtonPress, press, Qt::LeftButton,
                    Qt::LeftButton);
    send_ctrl_mouse(QEvent::MouseMove, crossed, Qt::NoButton, Qt::LeftButton);
    send_ctrl_mouse(QEvent::MouseMove, moved, Qt::NoButton, Qt::LeftButton);
    EXPECT_DOUBLE_EQ(amount->value(), displayed_amount);
    send_ctrl_mouse(QEvent::MouseButtonRelease, moved, Qt::LeftButton,
                    Qt::NoButton);
    EXPECT_FALSE(panel.numeric_interaction_active());

    const auto settings = editor.tools().active_settings();
    const auto *owner_amount = find_setting(settings, "extrude.amount");
    ASSERT_NE(owner_amount, nullptr);
    EXPECT_DOUBLE_EQ(owner_amount->number_value, displayed_amount);
    const auto mesh = editor.query().compiled_object_mesh(box.value());
    ASSERT_TRUE(mesh.has_value());
    float maximum_extent = 0.0F;
    for (const auto &vertex : mesh->vertices) {
      maximum_extent = std::max(
          {maximum_extent, std::abs(vertex.position.x),
           std::abs(vertex.position.y), std::abs(vertex.position.z)});
    }
    EXPECT_NEAR(maximum_extent, 1.0 + grid, 1.0e-5)
        << "one snapped grid step must move the cap by the physical world grid";
  }

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
          "Numeric Retention Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::RadialArray).ok());

  quader::QtToolSettingsPanel window;
  window.set_expanded(true);
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.bind(editor);
  window.show();
  QCoreApplication::processEvents();
  auto *copies = window.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.radial_array.copies"));
  auto *repeats = window.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.radial_array.repeats"));
  ASSERT_NE(copies, nullptr);
  ASSERT_NE(repeats, nullptr);
  ASSERT_NE(dynamic_cast<quader::QtSpinBox *>(copies), nullptr);
  ASSERT_TRUE(window.findChildren<QSlider *>().isEmpty());
  auto *copies_editor = copies->findChild<QLineEdit *>();
  ASSERT_NE(copies_editor, nullptr);
  auto &copies_controller = quader::qt_install_numeric_spin_box(*copies);
  QPointer<QSpinBox> retained_copies(copies);
  QPointer<QSpinBox> retained_repeats(repeats);

  const int local_copies_during_drag = copies->value();
  ASSERT_NE(local_copies_during_drag, 16);
  const QPoint copies_pointer = begin_numeric_scrub(*copies_editor);
  ASSERT_TRUE(copies_controller.is_dragging());
  ASSERT_TRUE(editor.tools()
                  .set_setting("radial_array", "radial_array.copies", 16.0)
                  .ok());
  window.refresh();
  EXPECT_EQ(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.radial_array.copies")),
            retained_copies.data());
  EXPECT_EQ(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.radial_array.repeats")),
            retained_repeats.data());
  EXPECT_TRUE(copies_controller.is_dragging());
  EXPECT_EQ(retained_copies->value(), local_copies_during_drag);
  EXPECT_EQ(retained_repeats->maximum(), 64);
  send_mouse_event(*copies_editor, QEvent::MouseButtonRelease, copies_pointer,
                   Qt::LeftButton, Qt::NoButton);
  copies->clearFocus();
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
     RadialChoiceButtonsPreserveDegreesAndRetainLocalizedInstanceSettings) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-radial-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.modeling().create_box(
      {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
      "Radial Settings Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::RadialArray).ok());
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "en-US"));

  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_localization(localization);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *total = panel.findChild<QPushButton *>(QStringLiteral(
      "tool_settings.control.radial_array.mode.option.total"));
  auto *spacing = panel.findChild<QPushButton *>(QStringLiteral(
      "tool_settings.control.radial_array.mode.option.spacing"));
  auto *instances = panel.findChild<QCheckBox *>(QStringLiteral(
      "tool_settings.control.radial_array.create_instances"));
  auto *degrees = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.radial_array.degrees"));
  auto *degrees_label = panel.findChild<QLabel *>(QStringLiteral(
      "tool_settings.control.radial_array.degrees.label"));
  auto *group = panel.findChild<QButtonGroup *>(QStringLiteral(
      "tool_settings.control.radial_array.mode.group"));
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  ASSERT_NE(total, nullptr);
  ASSERT_NE(spacing, nullptr);
  ASSERT_NE(instances, nullptr);
  ASSERT_NE(degrees, nullptr);
  ASSERT_NE(degrees_label, nullptr);
  ASSERT_NE(group, nullptr);
  ASSERT_NE(apply, nullptr);
  EXPECT_TRUE(group->exclusive());
  EXPECT_TRUE(total->isCheckable());
  EXPECT_TRUE(spacing->isCheckable());
  EXPECT_TRUE(total->isChecked());
  EXPECT_FALSE(spacing->isChecked());
  EXPECT_FALSE(instances->isChecked());
  EXPECT_FALSE(apply->isEnabled());
  EXPECT_EQ(degrees_label->text(), QStringLiteral("Total Angle (°)"));
  QPointer<QPushButton> retained_total(total);
  QPointer<QPushButton> retained_spacing(spacing);
  QPointer<QCheckBox> retained_instances(instances);
  QPointer<QSpinBox> retained_degrees(degrees);
  QPointer<QLabel> retained_degrees_label(degrees_label);
  const auto expect_retained_angle_controls = [&] {
    EXPECT_EQ(panel.findChild<QPushButton *>(total->objectName()), retained_total.data());
    EXPECT_EQ(panel.findChild<QPushButton *>(spacing->objectName()), retained_spacing.data());
    EXPECT_EQ(panel.findChild<QSpinBox *>(degrees->objectName()), retained_degrees.data());
    EXPECT_EQ(panel.findChild<QLabel *>(degrees_label->objectName()), retained_degrees_label.data());
    EXPECT_EQ(degrees->value(), 137);
  };

  // A mode click consumes the numeric draft before changing interpretation.
  degrees->setValue(137);
  spacing->click();
  panel.refresh();
  EXPECT_EQ(session.radial_array().mode(), quader::RadialArrayMode::Spacing);
  EXPECT_DOUBLE_EQ(session.radial_array().degrees(), 137.0);
  EXPECT_FALSE(total->isChecked());
  EXPECT_TRUE(spacing->isChecked());
  EXPECT_EQ(degrees_label->text(), QStringLiteral("Angle Spacing (°)"));
  expect_retained_angle_controls();
  total->click();
  panel.refresh();
  EXPECT_EQ(session.radial_array().mode(), quader::RadialArrayMode::Total);
  EXPECT_DOUBLE_EQ(session.radial_array().degrees(), 137.0);
  EXPECT_TRUE(total->isChecked());
  EXPECT_FALSE(spacing->isChecked());
  EXPECT_EQ(degrees_label->text(), QStringLiteral("Total Angle (°)"));
  expect_retained_angle_controls();
  const auto unchanged_settings = editor.tools().active_settings_revision();
  total->click();
  EXPECT_TRUE(total->isChecked());
  EXPECT_EQ(editor.tools().active_settings_revision(), unchanged_settings);

  ASSERT_TRUE(session.radial_array()
                  .update_center_candidate({}, {0.0f, 1.0f, 0.0f}, 1.0)
                  .ok);
  panel.refresh();
  EXPECT_TRUE(apply->isEnabled());
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  const auto preview_revision = session.radial_array().preview_state().revision;
  instances->click();
  panel.refresh();
  EXPECT_TRUE(session.radial_array().create_instances());
  EXPECT_TRUE(instances->isChecked());
  EXPECT_EQ(session.radial_array().preview_state().revision, preview_revision);
  EXPECT_EQ(panel.findChild<QPushButton *>(total->objectName()),
            retained_total.data());
  EXPECT_EQ(panel.findChild<QCheckBox *>(instances->objectName()),
            retained_instances.data());

  // Presentation sentinels expose redundant Qt writes even when owner setters
  // would independently reject an unchanged value.
  const auto retained_revision = editor.tools().active_settings_revision();
  {
    const QSignalBlocker block_total(total);
    const QSignalBlocker block_spacing(spacing);
    const QSignalBlocker block_instances(instances);
    spacing->setChecked(true);
    instances->setChecked(false);
  }
  degrees_label->setText(QStringLiteral("unchanged angle label sentinel"));
  for (int frame = 0; frame < 20; ++frame) {
    panel.refresh();
  }
  EXPECT_TRUE(spacing->isChecked());
  EXPECT_FALSE(instances->isChecked());
  EXPECT_EQ(degrees_label->text(), QStringLiteral("unchanged angle label sentinel"));
  EXPECT_EQ(editor.tools().active_settings_revision(), retained_revision);
  EXPECT_EQ(session.radial_array().preview_state().revision, preview_revision);
  {
    const QSignalBlocker block_total(total);
    const QSignalBlocker block_spacing(spacing);
    const QSignalBlocker block_instances(instances);
    total->setChecked(true);
    instances->setChecked(true);
  }
  degrees_label->setText(QStringLiteral("Total Angle"));

  for (const char *locale : {"it-IT", "en-US"}) {
    ASSERT_TRUE(localization.select_locale(locale));
    panel.set_localization(localization);
    total = panel.findChild<QPushButton *>(QStringLiteral(
        "tool_settings.control.radial_array.mode.option.total"));
    spacing = panel.findChild<QPushButton *>(QStringLiteral(
        "tool_settings.control.radial_array.mode.option.spacing"));
    instances = panel.findChild<QCheckBox *>(QStringLiteral(
        "tool_settings.control.radial_array.create_instances"));
    degrees = panel.findChild<QSpinBox *>(QStringLiteral(
        "tool_settings.control.radial_array.degrees"));
    degrees_label = panel.findChild<QLabel *>(QStringLiteral(
        "tool_settings.control.radial_array.degrees.label"));
    ASSERT_NE(total, nullptr);
    ASSERT_NE(spacing, nullptr);
    ASSERT_NE(instances, nullptr);
    ASSERT_NE(degrees, nullptr);
    ASSERT_NE(degrees_label, nullptr);
    const QPointer<QSpinBox> locale_degrees(degrees);
    const QPointer<QLabel> locale_label(degrees_label);
    const QPointer<QPushButton> locale_total(total);
    const QPointer<QPushButton> locale_spacing(spacing);
    for (const auto &[button, key] :
         {std::pair{total, "radial_array.mode.total"},
          std::pair{spacing, "radial_array.mode.spacing"}}) {
      EXPECT_EQ(button->text(),
                QString::fromStdString(localization.text(key, "missing")));
      EXPECT_EQ(button->toolTip(), QString::fromStdString(
          localization.text(std::string(key) + ".tooltip", "missing")));
      EXPECT_FALSE(button->toolTip().isEmpty());
    }
    auto *instances_label = panel.findChild<QLabel *>(QStringLiteral(
        "tool_settings.control.radial_array.create_instances.label"));
    ASSERT_NE(instances_label, nullptr);
    EXPECT_TRUE(instances->text().isEmpty());
    EXPECT_EQ(instances_label->text(), QString::fromStdString(
        localization.text("radial_array.create_instances", "missing")));
    EXPECT_EQ(instances->accessibleName(), instances_label->text());
    EXPECT_EQ(instances->toolTip(), QString::fromStdString(
        localization.text("radial_array.create_instances.tooltip", "missing")));
    EXPECT_TRUE(total->isChecked());
    EXPECT_FALSE(spacing->isChecked());
    EXPECT_TRUE(instances->isChecked());
    for (const auto &[button, label_key] :
         {std::pair{spacing, "radial_array.angle_spacing"},
          std::pair{total, "radial_array.total_angle"}}) {
      button->click();
      panel.refresh();
      EXPECT_EQ(degrees_label->text(), QString::fromStdString(
          localization.text(label_key, "missing")));
      EXPECT_EQ(degrees->value(), 137);
      EXPECT_DOUBLE_EQ(session.radial_array().degrees(), 137.0);
      EXPECT_EQ(panel.findChild<QSpinBox *>(degrees->objectName()), locale_degrees.data());
      EXPECT_EQ(panel.findChild<QLabel *>(degrees_label->objectName()), locale_label.data());
      EXPECT_EQ(panel.findChild<QPushButton *>(total->objectName()), locale_total.data());
      EXPECT_EQ(panel.findChild<QPushButton *>(spacing->objectName()), locale_spacing.data());
    }
  }

  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  panel.refresh();
  EXPECT_TRUE(total->isChecked());
  EXPECT_FALSE(instances->isChecked());
  EXPECT_DOUBLE_EQ(session.radial_array().degrees(), 360.0);
  panel.unbind();
  panel.hide();
}

TEST(QtToolSettingsPanelTests,
     SettingsOnlyOverlayStartsExpandedAndReopensForEachActivation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-overlay-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  EXPECT_FALSE(panel.has_settings());
  EXPECT_TRUE(panel.isHidden());
  EXPECT_TRUE(panel.expanded());
  EXPECT_EQ(panel.findChild<QWidget *>(
                QStringLiteral("tool_settings.description")), nullptr);
  EXPECT_EQ(panel.findChild<QWidget *>(
                QStringLiteral("tool_settings.section.description")), nullptr);
  EXPECT_EQ(panel.findChild<QWidget *>(
                QStringLiteral("tool_settings.section.settings")), nullptr);
  EXPECT_EQ(panel.findChild<QWidget *>(
                QStringLiteral("tool_settings.settings_empty")), nullptr);

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  panel.refresh();
  panel.show();
  auto *header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.target_title"));
  auto *body = panel.findChild<QScrollArea *>(
      QStringLiteral("tool_settings.scroll"));
  ASSERT_NE(header, nullptr);
  ASSERT_NE(body, nullptr);
  EXPECT_EQ(panel.findChild<QPushButton *>(
                QStringLiteral("tool_settings.reset_defaults")), nullptr);
  EXPECT_TRUE(panel.has_settings());
  EXPECT_EQ(panel.target_id(), "uv_peel");
  EXPECT_EQ(header->text(), QStringLiteral("UV Peel"));
  EXPECT_EQ(header->arrowType(), Qt::DownArrow);
  EXPECT_EQ(header->accessibleName(), header->text());
  EXPECT_EQ(panel.windowTitle(), header->text());
  EXPECT_FALSE(body->isHidden());
  EXPECT_TRUE(header->isChecked());
  header->click();
  EXPECT_FALSE(panel.expanded());
  EXPECT_TRUE(body->isHidden());
  const QSize collapsed_size = panel.sizeHint();
  EXPECT_EQ(collapsed_size.width(), 160);
  header->click();
  EXPECT_TRUE(panel.expanded());
  EXPECT_FALSE(body->isHidden());
  EXPECT_EQ(panel.sizeHint().width(), 320);
  EXPECT_GT(panel.sizeHint().height(), collapsed_size.height());

  auto *repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  ASSERT_NE(repeat, nullptr);
  repeat->setValue(3.0);
  finish_spin_edit(*repeat);
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  panel.refresh();
  QCoreApplication::processEvents();
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 1.0);

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  panel.refresh();
  EXPECT_FALSE(panel.has_settings());
  EXPECT_TRUE(panel.isHidden());
  EXPECT_TRUE(panel.expanded());
  EXPECT_TRUE(header->text().isEmpty());
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(
                QStringLiteral("tool_settings.control.uv_peel.u_repeat")), nullptr);

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  panel.refresh();
  EXPECT_TRUE(panel.has_settings());
  EXPECT_TRUE(panel.expanded());
  EXPECT_FALSE(body->isHidden());
  panel.show();
  EXPECT_EQ(header->text(), QString::fromStdString(editor.tools().active_settings().title));
  panel.set_expanded(false);
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  panel.refresh();
  EXPECT_TRUE(panel.expanded());
  EXPECT_FALSE(body->isHidden());
  panel.set_expanded(false);
  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "it-IT"));
  panel.set_localization(italian);
  EXPECT_EQ(header->text(), QStringLiteral("Distensione UV"));
  EXPECT_EQ(header->accessibleName(), header->text());
  EXPECT_FALSE(panel.expanded());
  panel.set_expanded(true);
  panel.unbind();
  EXPECT_TRUE(panel.isHidden());
  panel.bind(editor);
  EXPECT_TRUE(panel.has_settings());
  EXPECT_TRUE(panel.expanded());
}

TEST(QtToolSettingsPanelTests, ActiveToolWithoutSettingsHidesTheEntireOverlay) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-empty-settings-overlay-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = editor.modeling().create_box(
      {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      "Settings visibility");
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  ASSERT_TRUE(panel.has_settings());
  ASSERT_TRUE(panel.expanded());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Mirror).ok());
  const auto mirror_target = editor.tools().active_settings();
  ASSERT_EQ(editor.tools().active_tool(), "mirror");
  ASSERT_TRUE(mirror_target.controls.empty());
  panel.refresh();
  EXPECT_FALSE(panel.has_settings());
  EXPECT_TRUE(panel.target_id().empty());
  EXPECT_TRUE(panel.isHidden());
  EXPECT_EQ(panel.findChild<QWidget *>(
                QStringLiteral("tool_settings.control.auto_merge_vertices")), nullptr);
  auto *header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.target_title"));
  ASSERT_NE(header, nullptr);
  EXPECT_TRUE(header->text().isEmpty());
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     AutoMergeVisibilityTracksSelectionModeAndUnchangedRefreshRetainsTheControl) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-auto-merge-settings-visibility-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  int changes = 0;
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  EXPECT_FALSE(panel.has_settings());
  EXPECT_TRUE(panel.isHidden());

  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  for (const auto mode : {quader::QuaderSelectionMode::Edge,
                          quader::QuaderSelectionMode::Face,
                          quader::QuaderSelectionMode::Object,
                          quader::QuaderSelectionMode::Group}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
    panel.refresh();
    ASSERT_TRUE(panel.has_settings());
    panel.show();
    auto *toggle = panel.findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.auto_merge_vertices"));
    ASSERT_NE(toggle, nullptr);
    toggle->setChecked(false);
    panel.refresh();
    QCoreApplication::processEvents();
    EXPECT_FALSE(toggle->isChecked());
    const QPointer<QCheckBox> retained(toggle);
    TexturePresentationEvents events;
    toggle->installEventFilter(&events);
    panel.presentation_changed = [&] { ++changes; };
    changes = 0;
    const auto revision = editor.tools().active_settings_revision();
    for (int frame = 0; frame < 20; ++frame) panel.refresh();
    EXPECT_EQ(editor.tools().active_settings_revision(), revision);
    EXPECT_EQ(panel.findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.auto_merge_vertices")), retained.data());
    EXPECT_EQ(events.changes, 0);
    EXPECT_EQ(changes, 0);

    const bool object_mode = mode == quader::QuaderSelectionMode::Object ||
                             mode == quader::QuaderSelectionMode::Group;
    ASSERT_TRUE(editor.mode().set(object_mode ? quader::QuaderEditorMode::Object
                                              : quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode).ok());
    panel.refresh();
    EXPECT_FALSE(panel.has_settings());
    EXPECT_TRUE(panel.isHidden());
    EXPECT_TRUE(retained.isNull());
    EXPECT_EQ(panel.findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.auto_merge_vertices")), nullptr);
    const auto hidden_changes = changes;
    for (int frame = 0; frame < 20; ++frame) panel.refresh();
    EXPECT_EQ(changes, hidden_changes);
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
    panel.refresh();
    toggle = panel.findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.auto_merge_vertices"));
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isChecked());
  }
  panel.presentation_changed = {};
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     CollapseFlushesValidDraftsAndRestoresInvalidDraftsWithoutCompletingPreview) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-collapse-drafts-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.set_expanded(true);
  panel.show();
  QCoreApplication::processEvents();
  auto *repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  ASSERT_NE(repeat, nullptr);
  ASSERT_NE(apply, nullptr);
  const auto history_before = editor.history().snapshot();
  repeat->setLocale(QLocale::c());
  type_spin_text(*repeat, QStringLiteral("2.5"));
  panel.set_expanded(false);
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 2.5);
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  for (const QString &invalid : {QStringLiteral("0"), QStringLiteral("-")}) {
    panel.set_expanded(true);
    type_spin_text(*repeat, invalid);
    ASSERT_FALSE(apply->isEnabled());
    panel.set_expanded(false);
    EXPECT_FALSE(panel.numeric_interaction_active());
    EXPECT_DOUBLE_EQ(repeat->value(), 2.5);
    EXPECT_NE(repeat->text(), invalid);
    EXPECT_TRUE(apply->isEnabled());
    process_events_for(75);
    EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 2.5);
    EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
    EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  }
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     RepeatedOperationBeforeRefreshDiscardsOldNumericDispatchAndRetainsExpansion) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-operation-serial-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_expanded(true);
  panel.bind(editor);
  panel.show();
  auto *repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  ASSERT_NE(repeat, nullptr);
  const QPointer<QDoubleSpinBox> old_repeat(repeat);
  repeat->setValue(7.0);
  ASSERT_TRUE(panel.numeric_interaction_active());
  const auto serial = editor.tools().active_settings_presentation_origin().serial;
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  ASSERT_GT(editor.tools().active_settings_presentation_origin().serial, serial);
  panel.refresh();
  EXPECT_TRUE(old_repeat.isNull());
  EXPECT_TRUE(panel.expanded());
  EXPECT_FALSE(panel.numeric_interaction_active());
  process_events_for(75);
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 1.0);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     FocusTransferKeepsSettingsLifecycleBoundToTheSharedOperation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-pane-settings-lifecycle-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  ASSERT_TRUE(editor.viewports().focus_pane(quader::ViewportPaneId::Top).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  ASSERT_TRUE(editor.viewports().focus_pane(quader::ViewportPaneId::Perspective).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  const auto serial = editor.tools().active_settings_presentation_origin().serial;
  ASSERT_TRUE(editor.viewports().focus_pane(quader::ViewportPaneId::Top).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  ASSERT_EQ(panel.target_id(), "uv_peel");
  EXPECT_TRUE(panel.prepare_lifecycle_intent(
      editor, quader::QuaderNativeToolSettingsIntent::Apply));
  EXPECT_TRUE(panel.prepare_lifecycle_intent(
      editor, quader::QuaderNativeToolSettingsIntent::Cancel));
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_EQ(editor.tools().active_settings_presentation_origin().serial, serial);
  EXPECT_EQ(editor.viewports().selected_tool(quader::ViewportPaneId::Top),
            quader::ViewportToolId::UvPeel);
  EXPECT_EQ(editor.viewports().selected_tool(quader::ViewportPaneId::Perspective),
            quader::ViewportToolId::UvPeel);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     FocusTransferRetainsQueuedValuesForTheOriginOperation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-pane-settings-draft-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  ASSERT_NE(repeat, nullptr);
  const QPointer<QDoubleSpinBox> origin_repeat(repeat);
  const auto history_before = editor.history().snapshot();
  repeat->setValue(7.0);
  ASSERT_TRUE(panel.numeric_interaction_active());
  const auto origin = editor.tools().active_settings_presentation_origin();
  const auto active_revision = editor.tools().active_tool_revision();
  ASSERT_TRUE(editor.viewports().focus_pane(quader::ViewportPaneId::Top).ok());
  // Focus does not replace the settings operation, even before its next refresh.
  process_events_for(75);
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 7.0);
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_EQ(editor.tools().active_tool_revision(), active_revision);
  EXPECT_EQ(editor.tools().active_settings_presentation_origin(), origin);
  panel.refresh();
  EXPECT_TRUE(panel.has_settings());
  EXPECT_EQ(panel.target_id(), "uv_peel");
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat")), origin_repeat.data());
  EXPECT_FALSE(panel.numeric_interaction_active());
  ASSERT_TRUE(editor.viewports().focus_pane(quader::ViewportPaneId::Perspective).ok());
  panel.refresh();
  EXPECT_EQ(panel.target_id(), "uv_peel");
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 7.0);
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     StaleChoicesAndLifecycleButtonsCannotChangeAReplacementOperation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-stale-controls-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_expanded(true);
  panel.bind(editor);
  panel.show();
  auto *axis = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.uv_peel.axis"));
  auto *world = panel.findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.uv_peel.world_space"));
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  auto *cancel = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(axis, nullptr);
  ASSERT_NE(world, nullptr);
  ASSERT_NE(cancel, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_TRUE(apply->isEnabled());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  auto options = editor.modeling().uv_peel_settings().options;
  options.u_repeat = 3.0;
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(options).ok());
  axis->setCurrentIndex(1);
  world->click();
  apply->click();
  cancel->click();
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  panel.refresh();
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     PreferredSizeScalesAndUnchangedRefreshDoesNotNotifyTheViewportHost) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-overlay-size-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  EXPECT_EQ(panel.sizeHint().width(), 320);
  panel.set_expanded(false);
  EXPECT_EQ(panel.sizeHint().width(), 160);
  panel.apply_ui_scale(quader::QtUiScaleMetrics(2.0F, 1U));
  EXPECT_EQ(panel.sizeHint().width(), 320);
  panel.set_expanded(true);
  EXPECT_EQ(panel.sizeHint().width(), 640);
  panel.apply_ui_scale(quader::QtUiScaleMetrics(1.0F, 2U));
  EXPECT_EQ(panel.sizeHint().width(), 320);
  panel.resize(180, 180);
  panel.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(panel.width(), 180);
  auto *scroll = panel.findChild<QScrollArea *>(
      QStringLiteral("tool_settings.scroll"));
  auto *actions = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.actions"));
  ASSERT_NE(scroll, nullptr);
  ASSERT_NE(actions, nullptr);
  EXPECT_TRUE(panel.rect().contains(actions->geometry()));
  EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
  int changes = 0;
  panel.presentation_changed = [&] { ++changes; };
  for (int frame = 0; frame < 20; ++frame) {
    panel.refresh();
  }
  EXPECT_EQ(changes, 0);
  panel.set_expanded(false);
  EXPECT_EQ(changes, 1);
  panel.unbind();
  EXPECT_EQ(changes, 2);
}

TEST(QtFaceTextureControlsTests,
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
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());

  quader::QtFaceTextureControls panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.resize(330, 650);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *section = panel.findChild<QWidget *>(
      QStringLiteral("face_texture.section.texture"));
  auto *description_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.description"));
  auto *settings_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.settings"));
  auto *header = panel.findChild<QToolButton *>(
      QStringLiteral("face_texture.section.texture.header"));
  auto *content = panel.findChild<QWidget *>(
      QStringLiteral("face_texture.section.texture.content"));
  auto *scale_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  auto *scale_y = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_y"));
  auto *shift_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.shift_x"));
  auto *shift_y = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.shift_y"));
  auto *rotate = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.rotate"));
  auto *target_title = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.target_title"));
  auto *title_separator = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.title_separator"));
  auto *edit_section = panel.findChild<QWidget *>(
      QStringLiteral("face_texture.section.edit_texture"));
  auto *edit_header = panel.findChild<QToolButton *>(
      QStringLiteral("face_texture.section.edit_texture.header"));
  auto *edit_content = panel.findChild<QWidget *>(
      QStringLiteral("face_texture.section.edit_texture.content"));
  auto *treat_as_one = panel.findChild<QCheckBox *>(
      QStringLiteral("face_texture.edit_texture.treat_as_one"));
  auto *align_to_face = panel.findChild<QPushButton *>(
      QStringLiteral("face_texture.edit_texture.align_to_face"));
  ASSERT_NE(section, nullptr);
  EXPECT_EQ(description_section, nullptr);
  EXPECT_EQ(settings_section, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(scale_x, nullptr);
  ASSERT_NE(scale_y, nullptr);
  ASSERT_NE(shift_x, nullptr);
  ASSERT_NE(shift_y, nullptr);
  ASSERT_NE(rotate, nullptr);
  EXPECT_EQ(target_title, nullptr);
  EXPECT_EQ(title_separator, nullptr);
  ASSERT_NE(edit_section, nullptr);
  ASSERT_NE(edit_header, nullptr);
  ASSERT_NE(edit_content, nullptr);
  ASSERT_NE(treat_as_one, nullptr);
  ASSERT_NE(align_to_face, nullptr);
  EXPECT_EQ(shift_x->accessibleName(), QStringLiteral("Shift U (px)"));
  EXPECT_EQ(shift_y->accessibleName(), QStringLiteral("Shift V (px)"));
  ASSERT_EQ(scale_y->locale().decimalPoint(), QStringLiteral("."));
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  EXPECT_TRUE(edit_header->isChecked());
  EXPECT_FALSE(treat_as_one->isChecked());
  EXPECT_EQ(edit_section->findChildren<QPushButton *>().size(), 17);
  for (const char *removed : {"scale_x_up", "scale_x_down", "scale_y_up",
                               "scale_y_down", "shift_left", "shift_right",
                               "shift_up", "shift_down"}) {
    EXPECT_EQ(panel.findChild<QPushButton *>(
                  QStringLiteral("face_texture.edit_texture.%1")
                      .arg(QString::fromLatin1(removed))), nullptr);
  }
  EXPECT_TRUE(header->isChecked());
  EXPECT_TRUE(section->findChildren<QSlider *>().isEmpty());
  quader::QtToolSettingsPanel tool_panel;
  tool_panel.set_expanded(true);
  tool_panel.bind(editor);
  EXPECT_EQ(tool_panel.findChild<QDoubleSpinBox *>(
                QStringLiteral("face_texture.texture.scale_x")), nullptr);
  EXPECT_EQ(tool_panel.findChild<QWidget *>(
                QStringLiteral("tool_settings.section.texture")), nullptr);
  const QPointer<QDoubleSpinBox> retained_scale_y(scale_y);

  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleY, 2.0).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleY, 4.0).ok());
  ASSERT_TRUE(editor.selection().select_faces(source.value(), {1, 2}).ok());
  panel.refresh();
  // Wrapped labels and numeric minima publish queued layout changes. Drain the
  // same bounded layout cycle used by the scale-link presentation tests.
  auto *dispatcher = QAbstractEventDispatcher::instance();
  ASSERT_NE(dispatcher, nullptr);
  bool layout_settled = false;
  for (int iteration = 0; iteration < 64; ++iteration) {
    if (!dispatcher->processEvents(QEventLoop::AllEvents)) {
      layout_settled = true;
      break;
    }
  }
  ASSERT_TRUE(layout_settled);
  const auto numeric_bounds = [content](const QWidget *widget) {
    return QRect(widget->mapTo(content, QPoint()), widget->size());
  };
  EXPECT_EQ(scale_x->geometry().top(), scale_y->geometry().top());
  EXPECT_LT(scale_x->geometry().right(), scale_y->geometry().left());
  EXPECT_EQ(shift_x->geometry().top(), shift_y->geometry().top());
  EXPECT_LT(shift_x->geometry().right(), shift_y->geometry().left());
  EXPECT_LT(scale_x->geometry().bottom(), shift_x->geometry().top());
  EXPECT_LT(numeric_bounds(shift_x).bottom(), numeric_bounds(rotate).top());
  EXPECT_EQ(scale_x->geometry().left(), shift_x->geometry().left());
  EXPECT_EQ(scale_x->geometry().right(), shift_x->geometry().right());
  EXPECT_EQ(scale_y->geometry().left(), shift_y->geometry().left());
  EXPECT_EQ(scale_y->geometry().right(), shift_y->geometry().right());
  // Rotation owns a full-width row and a readable, independently sized label.
  EXPECT_GE(numeric_bounds(rotate).left(), numeric_bounds(scale_x).left());
  EXPECT_EQ(numeric_bounds(rotate).right(), numeric_bounds(scale_y).right());
  for (QDoubleSpinBox *spin : {scale_x, scale_y, shift_x, shift_y, rotate}) {
    EXPECT_TRUE(content->rect().contains(numeric_bounds(spin)));
  }
  TexturePresentationEvents heading_events;
  header->installEventFilter(&heading_events);
  edit_header->installEventFilter(&heading_events);
  for (int frame = 0; frame < 20; ++frame) {
    panel.refresh();
  }
  EXPECT_EQ(heading_events.changes, 0);
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);
  EXPECT_EQ(scale_y->text(), QStringLiteral("--"));
  EXPECT_TRUE(scale_y->isEnabled());
  EXPECT_EQ(shift_x->text(), QStringLiteral("--"));
  EXPECT_EQ(shift_y->text(), QStringLiteral("--"));
  EXPECT_FALSE(shift_x->isEnabled());
  EXPECT_FALSE(shift_y->isEnabled());
  EXPECT_TRUE(align_to_face->isEnabled());
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
  EXPECT_FALSE(quader::qt_install_numeric_spin_box(*scale_y).is_editing());
  // Tab may start the next numeric editor's own draft. Settle that receiver
  // before checking that the entire panel has no active numeric interaction.
  ASSERT_NE(QApplication::focusWidget(), nullptr);
  QApplication::focusWidget()->clearFocus();
  QCoreApplication::processEvents();
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
  panel.refresh();
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
  EXPECT_FALSE(quader::qt_install_numeric_spin_box(*scale_y).is_editing());
  ASSERT_NE(QApplication::focusWidget(), nullptr);
  QApplication::focusWidget()->clearFocus();
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel.numeric_interaction_active());

  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  panel.refresh();
  EXPECT_TRUE(shift_x->isEnabled());
  EXPECT_TRUE(shift_y->isEnabled());
  EXPECT_DOUBLE_EQ(scale_x->value(), 5.0);
  EXPECT_DOUBLE_EQ(scale_y->value(), 0.5);
  shift_x->setValue(32.0);
  finish_spin_edit(*shift_x);
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().shift_x, 32.0);
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  panel.refresh();
  EXPECT_DOUBLE_EQ(shift_x->value(), 0.0);
  EXPECT_DOUBLE_EQ(scale_x->value(), 5.0);
  EXPECT_DOUBLE_EQ(scale_y->value(), 0.5);

  header->click();
  EXPECT_TRUE(content->isHidden());
  ASSERT_TRUE(editor.selection().clear().ok());
  panel.refresh();
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  EXPECT_TRUE(content->isHidden());
  EXPECT_TRUE(edit_content->isHidden());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  panel.refresh();
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  EXPECT_TRUE(edit_content->isHidden());
  EXPECT_TRUE(treat_as_one->isChecked());
  EXPECT_TRUE(content->isHidden());
  EXPECT_EQ(retained_scale_y.data(), scale_y);
  EXPECT_FALSE(header->isChecked());

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      "it-IT"));
  panel.set_localization(italian);
  EXPECT_EQ(header->text(), QStringLiteral("Impostazioni texture"));
  EXPECT_EQ(edit_header->text(), QStringLiteral("Modifica texture"));
  EXPECT_EQ(treat_as_one->text(), QStringLiteral("Tratta come unico"));
  EXPECT_FALSE(align_to_face->accessibleName().isEmpty());
  EXPECT_TRUE(align_to_face->toolTip().startsWith(align_to_face->accessibleName()));
  EXPECT_EQ(scale_x->accessibleName(), QStringLiteral("Scala U"));
  EXPECT_EQ(shift_x->accessibleName(), QStringLiteral("Spostamento U (px)"));
  EXPECT_EQ(shift_y->accessibleName(), QStringLiteral("Spostamento V (px)"));
  EXPECT_EQ(scale_y->locale().decimalPoint(), QStringLiteral("."));
  EXPECT_TRUE(content->isHidden());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Object).ok());
  panel.refresh();
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
  panel.unbind();
  EXPECT_TRUE(section->isHidden());
  EXPECT_TRUE(edit_section->isHidden());
}

TEST(QtFaceTextureControlsTests,
     EveryEditTextureButtonMatchesItsPublicApiAction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-edit-texture-bindings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  using Action = quader::QuaderFaceTextureAction;
  constexpr std::array<std::pair<const char *, Action>, 17> kCases{{
      {"align_to_grid", Action::AlignToGrid},
      {"align_to_face", Action::AlignToFace},
      {"align_to_view", Action::AlignToView},
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
      ASSERT_TRUE(api->selection().select_object(created.value()).ok());
      ASSERT_TRUE(api->mode().set(quader::QuaderEditorMode::Edit).ok());
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
    quader::QtFaceTextureControls panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    auto *button = panel.findChild<QPushButton *>(
        QStringLiteral("face_texture.edit_texture.%1").arg(
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

TEST(QtFaceTextureControlsTests,
     TextureScaleSpinnersUseHundredthsWithFixedPrecisionAndRetainHistory) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-texture-hundredth-scale-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  using Field = quader::QuaderFaceTextureField;
  const std::array<std::pair<const char *, Field>, 2> axes{{
      {"x", Field::ScaleX}, {"y", Field::ScaleY},
  }};
  for (const auto &[axis, field] : axes) {
    SCOPED_TRACE(axis);
    auto standalone = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    const auto created = create_texture_action_surface(editor);
    ASSERT_TRUE(created.ok()) << created.error_message();
    ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
    ASSERT_TRUE(set_face_texture_field(editor, field, 1.0).ok());
    quader::QtFaceTextureControls panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    auto *spin = panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("face_texture.texture.scale_%1")
            .arg(QString::fromLatin1(axis)));
    auto *message = panel.findChild<QLabel *>(
        QStringLiteral("face_texture.edit_texture.message"));
    ASSERT_NE(spin, nullptr);
    ASSERT_NE(message, nullptr);
    EXPECT_DOUBLE_EQ(spin->singleStep(), 0.01);
    const auto expect_scale = [&](double expected, const char *text) {
      const auto settings = editor.modeling().face_texture_settings();
      const auto actual = field == Field::ScaleX ? settings.scale_x
                                                 : settings.scale_y;
      ASSERT_TRUE(actual.has_value());
      EXPECT_NEAR(*actual, expected, 0.000001);
      EXPECT_EQ(field == Field::ScaleX ? settings.scale_y : settings.scale_x,
                std::optional<double>(1.0));
      EXPECT_NEAR(spin->value(), expected, 0.000001);
      EXPECT_EQ(spin->text(), QString::fromLatin1(text));
      EXPECT_TRUE(message->isHidden());
    };
    const auto step = [&](bool upward) {
      if (upward) {
        spin->stepUp();
      } else {
        spin->stepDown();
      }
      finish_spin_edit(*spin);
    };
    expect_scale(1.0, "1.0000");
    step(true);
    expect_scale(1.01, "1.0100");
    send_key_event(*spin, Qt::Key_Up);
    finish_spin_edit(*spin);
    expect_scale(1.02, "1.0200");
    ASSERT_TRUE(editor.history().undo().ok());
    panel.refresh();
    expect_scale(1.01, "1.0100");
    ASSERT_TRUE(editor.history().redo().ok());
    panel.refresh();
    expect_scale(1.02, "1.0200");
    step(false);
    expect_scale(1.01, "1.0100");
    send_key_event(*spin, Qt::Key_Down);
    finish_spin_edit(*spin);
    expect_scale(1.0, "1.0000");
    ASSERT_TRUE(editor.history().undo().ok());
    panel.refresh();
    expect_scale(1.01, "1.0100");
    ASSERT_TRUE(editor.history().redo().ok());
    panel.refresh();
    expect_scale(1.0, "1.0000");

    ASSERT_TRUE(set_face_texture_field(editor, field, 0.01).ok());
    panel.refresh();
    const auto before_zero = editor.revisions().modeling_content_revision;
    // Re-enter the exact decimal before stepping: the model's float storage
    // can otherwise leave a tiny remainder after subtracting 0.01.
    type_spin_text(*spin, QStringLiteral("0.01"));
    spin->stepDown();
    EXPECT_DOUBLE_EQ(spin->value(), 0.0);
    finish_spin_edit(*spin);
    expect_scale(0.01, "0.0100");
    EXPECT_EQ(editor.revisions().modeling_content_revision, before_zero);
    panel.unbind();
  }
}

TEST(QtFaceTextureControlsTests,
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
    ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
    ASSERT_TRUE(set_face_texture_field(editor, field, 4.0).ok());
    ASSERT_TRUE(editor.selection().select_face(created.value(), 2).ok());
    ASSERT_TRUE(set_face_texture_field(editor, field, 8.0).ok());
    ASSERT_TRUE(editor.selection().select_faces(created.value(), {1, 2}).ok());
    quader::QtFaceTextureControls panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    auto *spin = panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("face_texture.texture.scale_%1")
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
      const auto actual = field == Field::ScaleX ? settings.scale_x
                                                 : settings.scale_y;
      ASSERT_TRUE(actual.has_value());
      EXPECT_NEAR(*actual, upward ? 1.01 : 0.99, 0.000001);
      EXPECT_EQ(spin->text(), upward ? QStringLiteral("1.0100")
                                    : QStringLiteral("0.9900"));
      ASSERT_TRUE(editor.history().undo().ok());
      panel.refresh();
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
      panel.refresh();
    }
    panel.unbind();
  }
}

TEST(QtFaceTextureControlsTests,
     TextureScaleManualValuesRemainUnrestrictedAndArrowsAddToNegativeValues) {
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
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  quader::QtFaceTextureControls panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  for (const bool x_axis : {true, false}) {
    SCOPED_TRACE(x_axis ? "x" : "y");
    auto *spin = panel.findChild<QDoubleSpinBox *>(
        x_axis ? QStringLiteral("face_texture.texture.scale_x")
               : QStringLiteral("face_texture.texture.scale_y"));
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
    ASSERT_TRUE(stored_value().has_value());
    EXPECT_NEAR(*stored_value(), -1.49, 0.000001);
    EXPECT_EQ(spin->text(), QStringLiteral("-1.4900"));
    spin->stepDown();
    finish_spin_edit(*spin);
    ASSERT_TRUE(stored_value().has_value());
    EXPECT_NEAR(*stored_value(), -1.5, 0.000001);
    EXPECT_EQ(spin->text(), QStringLiteral("-1.5000"));
  }
  panel.unbind();
}

TEST(QtFaceTextureControlsTests,
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
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  std::vector<quader::QuaderNotification> notifications;
  editor.ui().set_runtime_hooks({.notify = [&](quader::QuaderNotification value) {
    notifications.push_back(std::move(value));
    return quader::QuaderApiVoidResult{.ok = true};
  }});
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  quader::QtFaceTextureControls panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *scale = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  auto *mirror = panel.findChild<QPushButton *>(
      QStringLiteral("face_texture.edit_texture.flip_texture_horizontal"));
  auto *rotate = panel.findChild<QPushButton *>(
      QStringLiteral("face_texture.edit_texture.rotate_clockwise"));
  auto *message = panel.findChild<QLabel *>(
      QStringLiteral("face_texture.edit_texture.message"));
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(mirror, nullptr);
  ASSERT_NE(rotate, nullptr);
  ASSERT_NE(message, nullptr);
  auto *error_message = dynamic_cast<quader::QtStatusMessage *>(message);
  ASSERT_NE(error_message, nullptr);
  int inline_announcements = 0;
  error_message->set_error_sound_callback([&] { ++inline_announcements; });

  type_spin_text(*scale, QStringLiteral("2.0"));
  ASSERT_TRUE(panel.numeric_interaction_active());
  mirror->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(-2.0));
  EXPECT_FALSE(panel.numeric_interaction_active());
  process_events_for(75);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(-2.0));
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(2.0));
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  ASSERT_TRUE(editor.history().can_redo());

  // A real mouse click must not let Qt silently restore zero to the previous
  // value on focus-out and then execute an action the user could not see.
  EXPECT_TRUE(notifications.empty());
  type_spin_text(*scale, QStringLiteral("0"));
  const auto invalid_revision = editor.revisions().modeling_content_revision;
  send_mouse_event(*mirror, QEvent::MouseButtonPress, mirror->rect().center(),
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*mirror, QEvent::MouseButtonRelease, mirror->rect().center(),
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(editor.history().can_redo());
  EXPECT_TRUE(panel.numeric_interaction_active());
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(error_message->is_error());
  ASSERT_EQ(notifications.size(), 1u);
  EXPECT_EQ(notifications.back().level, quader::QuaderNotificationLevel::Error);
  EXPECT_FALSE(notifications.back().message.empty());
  panel.refresh();
  process_events_for(75);
  EXPECT_EQ(notifications.size(), 1u);
  EXPECT_EQ(inline_announcements, 0);
  type_spin_text(*scale, QStringLiteral("1"));
  // The correction is dispatched by a timer even though the stored value is
  // unchanged. Wait for its observable completion, not another timer's expiry.
  process_events_for(75);
  EXPECT_TRUE(message->isHidden());
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(editor.history().can_redo());
  type_spin_text(*scale, QStringLiteral("-"));
  send_mouse_event(*mirror, QEvent::MouseButtonPress, mirror->rect().center(),
                   Qt::LeftButton, Qt::LeftButton);
  send_mouse_event(*mirror, QEvent::MouseButtonRelease, mirror->rect().center(),
                   Qt::LeftButton, Qt::NoButton);
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(editor.history().can_redo());
  EXPECT_TRUE(message->isHidden());
  type_spin_text(*scale, QStringLiteral("3.0"));
  mirror->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(-3.0));
  EXPECT_TRUE(message->isHidden());

  scale->setValue(7.0);
  ASSERT_TRUE(editor.selection().select_face(created.value(), 2).ok());
  const auto stale_revision = editor.revisions().modeling_content_revision;
  mirror->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, stale_revision);
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_TRUE(message->isHidden());
  process_events_for(75);
  EXPECT_EQ(editor.revisions().modeling_content_revision, stale_revision);
  // The rejected click refreshed the presentation, so a deliberate second
  // click now belongs to the newly selected face.
  mirror->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(-1.0));

  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleX,
                  1.0 / 8192.0).ok());
  panel.refresh();
  const auto failure_revision = editor.revisions().modeling_content_revision;
  const auto notifications_before_failure = notifications.size();
  editor.preferences().set_runtime_hooks({
      .rotation_snap_degrees = [] { return 0.0; },
  });
  rotate->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, failure_revision);
  EXPECT_TRUE(message->isHidden());
  EXPECT_FALSE(error_message->is_error());
  ASSERT_EQ(notifications.size(), notifications_before_failure + 1u);
  EXPECT_EQ(notifications.back().level, quader::QuaderNotificationLevel::Error);
  EXPECT_FALSE(notifications.back().message.empty());
  panel.refresh();
  process_events_for(75);
  EXPECT_EQ(notifications.size(), notifications_before_failure + 1u);
  EXPECT_EQ(inline_announcements, 0);

  editor.preferences().set_runtime_hooks({
      .rotation_snap_degrees = [] { return 15.0; },
  });
  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  panel.refresh();
  EXPECT_FALSE(mirror->isEnabled());
  EXPECT_FALSE(rotate->isEnabled());
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(error_message->is_error());
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  panel.refresh();
  EXPECT_TRUE(mirror->isEnabled());
  EXPECT_TRUE(message->isHidden());
  panel.unbind();
}

TEST(QtFaceTextureControlsTests,
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
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());

  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.resize(600, 1000);
  window.setCentralWidget(new QWidget(&window));
  auto *dock = new QDockWidget(&window);
  auto *panel = new quader::QtFaceTextureControls(dock);
  panel->setFixedWidth(360);
  dock->setWidget(panel);
  window.addDockWidget(Qt::RightDockWidgetArea, dock);
  panel->bind(editor);
  window.show();
  QCoreApplication::processEvents();
  auto *section = panel->findChild<QWidget *>(
      QStringLiteral("face_texture.section.edit_texture"));
  auto *content = panel->findChild<QWidget *>(
      QStringLiteral("face_texture.section.edit_texture.content"));
  auto *header = panel->findChild<QToolButton *>(
      QStringLiteral("face_texture.section.edit_texture.header"));
  auto *scale = panel->findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  auto *rotate = panel->findChild<QPushButton *>(
      QStringLiteral("face_texture.edit_texture.rotate_clockwise"));
  auto *treat_as_one = panel->findChild<QCheckBox *>(
      QStringLiteral("face_texture.edit_texture.treat_as_one"));
  ASSERT_NE(section, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(scale, nullptr);
  ASSERT_NE(rotate, nullptr);
  ASSERT_NE(treat_as_one, nullptr);
  const auto buttons = section->findChildren<QPushButton *>();
  ASSERT_EQ(buttons.size(), 17);
  const std::array<std::pair<const char *, std::vector<const char *>>, 6> rows{{
      {"align", {"align_to_grid", "align_to_face", "align_to_view"}},
      {"rotate", {"rotate_clockwise", "rotate_counter_clockwise"}},
      {"flip_texture", {"flip_texture_horizontal", "flip_texture_vertical"}},
      {"flip_face_uvs", {"flip_face_horizontal", "flip_face_vertical"}},
      {"fit", {"fit_both", "fit_x", "fit_y"}},
      {"justify", {"justify_left", "justify_right", "justify_top",
                     "justify_bottom", "justify_center"}},
  }};
  int previous_bottom = -1;
  for (const auto &[group, actions] : rows) {
    auto *label = content->findChild<QLabel *>(
        QStringLiteral("face_texture.edit_texture.group.%1")
            .arg(QString::fromLatin1(group)));
    ASSERT_NE(label, nullptr);
    int row_top = -1;
    for (const char *action : actions) {
      auto *button = content->findChild<QPushButton *>(
          QStringLiteral("face_texture.edit_texture.%1")
              .arg(QString::fromLatin1(action)));
      ASSERT_NE(button, nullptr);
      if (row_top < 0) row_top = button->geometry().top();
      EXPECT_EQ(button->geometry().top(), row_top);
      EXPECT_GT(button->geometry().left(), label->geometry().right());
      EXPECT_LE(std::abs(button->geometry().center().y() -
                           label->geometry().center().y()), 1);
    }
    EXPECT_GT(row_top, previous_bottom);
    previous_bottom = label->geometry().bottom();
  }
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
  panel->refresh();
  EXPECT_EQ(scale->text(), QStringLiteral("5.0"));
  EXPECT_TRUE(rotate->toolTip().contains(QStringLiteral("22.5")));
  ASSERT_TRUE(editor.preferences().set_shortcut(
                  quader::quader_command::kModelingTextureRotateClockwise,
                  "Ctrl+Alt+Shift+F12").ok());
  panel->refresh();
  EXPECT_TRUE(rotate->toolTip().contains(QStringLiteral("F12")));
  EXPECT_EQ(scale->text(), QStringLiteral("5.0"));
  rotate->click();
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(5.0));
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees,
              editor.modeling().face_texture_canvas().rotation_degrees, 1.0e-5);
  EXPECT_FALSE(panel->numeric_interaction_active());
  process_events_for(75);
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees,
              editor.modeling().face_texture_canvas().rotation_degrees, 1.0e-5);

  TexturePresentationEvents presentation_events;
  for (QPushButton *button : buttons) {
    button->installEventFilter(&presentation_events);
  }
  for (int frame = 0; frame < 20; ++frame) {
    panel->refresh();
  }
  EXPECT_EQ(presentation_events.changes, 0);

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "it-IT"));
  const QPointer<QPushButton> retained_rotate(rotate);
  panel->set_localization(italian);
  QCoreApplication::processEvents();
  EXPECT_EQ(header->text(), QStringLiteral("Modifica texture"));
  EXPECT_EQ(rotate->text(), QStringLiteral("\u21BB"));
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
  panel->refresh();
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  panel->refresh();
  EXPECT_TRUE(dock->isHidden());
  EXPECT_FALSE(section->isHidden());
  EXPECT_TRUE(content->isHidden());
  EXPECT_FALSE(header->isChecked());
  panel->unbind();
}

TEST(QtToolSettingsPanelTests,
     UvPeelActiveSettingsRetainSceneOptionsLocaleAndCollapsedState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  const QLocale previous_locale;
  const auto restore_locale =
      qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
  QLocale::setDefault(QLocale::c());
  int argc = 1;
  char executable[] = "quader-uv-peel-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());

  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.resize(600, 900);
  window.setCentralWidget(new QWidget(&window));
  auto *panel = new quader::QtToolSettingsPanel(window.centralWidget());
  panel->set_expanded(true);
  panel->resize(280, 700);
  panel->bind(editor);
  window.show();
  QCoreApplication::processEvents();
  EXPECT_EQ(panel->findChild<QWidget *>(
                QStringLiteral("tool_settings.section.uv_peel")), nullptr);
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  panel->refresh();
  EXPECT_FALSE(panel->has_active_target());
  EXPECT_EQ(panel->findChild<QComboBox *>(
                QStringLiteral("tool_settings.control.uv_peel.axis")), nullptr);

  const auto original = editor.modeling().mesh(created.value()).compiled_mesh();
  ASSERT_TRUE(original.has_value());
  const auto history_before = editor.history().snapshot();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  panel->refresh();
  panel->show();
  QCoreApplication::processEvents();
  auto *section = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.content"));
  auto *header = panel->findChild<QToolButton *>(
      QStringLiteral("tool_settings.target_title"));
  auto *content = panel->findChild<QWidget *>(
      QStringLiteral("tool_settings.scroll"));
  auto *axis = panel->findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.uv_peel.axis"));
  auto *world_space = panel->findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.uv_peel.world_space"));
  auto *apply = panel->findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  auto *cancel = panel->findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(section, nullptr);
  ASSERT_NE(header, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(axis, nullptr);
  ASSERT_NE(world_space, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  EXPECT_TRUE(panel->has_active_target());
  EXPECT_FALSE(section->isHidden());
  EXPECT_EQ(editor.tools().active_settings().tool_id, "uv_peel");
  EXPECT_EQ(editor.tools().active_settings().controls.size(), 6u);
  EXPECT_EQ(axis->currentIndex(), 0);
  EXPECT_TRUE(world_space->isChecked());
  EXPECT_TRUE(apply->isEnabled());

  axis->setCurrentIndex(1);
  world_space->click();
  QCoreApplication::processEvents();
  constexpr std::array<const char *, 4> kFields{
      "u_repeat", "v_repeat", "u_offset", "v_offset"};
  constexpr std::array<double, 4> kValues{2.0, 3.0, 0.25, -0.5};
  std::array<QDoubleSpinBox *, 4> spins{};
  for (std::size_t index = 0; index < kFields.size(); ++index) {
    const QString name = QStringLiteral("tool_settings.control.uv_peel.%1")
                             .arg(QString::fromLatin1(kFields[index]));
    spins[index] = panel->findChild<QDoubleSpinBox *>(name);
    ASSERT_NE(spins[index], nullptr);
    EXPECT_EQ(panel->findChild<QSlider *>(name + QStringLiteral(".slider")),
              nullptr);
    EXPECT_DOUBLE_EQ(spins[index]->singleStep(), 0.1);
    EXPECT_LT(spins[index]->minimum(), -1.0e30);
    EXPECT_GT(spins[index]->maximum(), 1.0e30);
    spins[index]->setValue(kValues[index]);
  }
  const quader::QuaderUvPeelOptions expected{
      .axis = quader::QuaderUvPeelAxis::V, .world_space = false,
      .u_repeat = 2.0, .v_repeat = 3.0, .u_offset = 0.25, .v_offset = -0.5};
  ASSERT_TRUE(process_events_until([&] {
    return editor.modeling().uv_peel_settings().options == expected;
  }, 1000));
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  for (QDoubleSpinBox *spin : spins) {
    finish_spin_edit(*spin);
  }
  panel->refresh();
  QCoreApplication::processEvents();
  EXPECT_FALSE(panel->numeric_interaction_active());

  const QPointer<QDoubleSpinBox> retained_repeat(spins[0]);
  const auto content_revision = editor.revisions().modeling_content_revision;
  const auto settings_revision = editor.tools().active_settings_revision();
  TexturePresentationEvents presentation_events;
  for (QWidget *widget : section->findChildren<QWidget *>()) {
    widget->installEventFilter(&presentation_events);
  }
  for (int frame = 0; frame < 20; ++frame) {
    panel->refresh();
  }
  EXPECT_EQ(presentation_events.changes, 0);
  EXPECT_EQ(editor.revisions().modeling_content_revision, content_revision);
  EXPECT_EQ(editor.tools().active_settings_revision(), settings_revision);
  EXPECT_EQ(panel->findChild<QDoubleSpinBox *>(
                QStringLiteral("tool_settings.control.uv_peel.u_repeat")),
            retained_repeat.data());

  quader::LocalizationService italian;
  ASSERT_TRUE(italian.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "it-IT"));
  panel->set_localization(italian);
  QCoreApplication::processEvents();
  axis = panel->findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.uv_peel.axis"));
  world_space = panel->findChild<QCheckBox *>(
      QStringLiteral("tool_settings.control.uv_peel.world_space"));
  auto *repeat = panel->findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  ASSERT_NE(axis, nullptr);
  ASSERT_NE(world_space, nullptr);
  ASSERT_NE(repeat, nullptr);
  EXPECT_EQ(axis->accessibleName(), QStringLiteral("Asse"));
  auto *world_space_label = panel->findChild<QLabel *>(QStringLiteral(
      "tool_settings.control.uv_peel.world_space.label"));
  ASSERT_NE(world_space_label, nullptr);
  EXPECT_TRUE(world_space->text().isEmpty());
  EXPECT_EQ(world_space_label->text(), QStringLiteral("Densit\u00E0 globale"));
  EXPECT_EQ(world_space->accessibleName(), world_space_label->text());
  EXPECT_EQ(repeat->accessibleName(), QStringLiteral("Ripetizioni U"));
  EXPECT_DOUBLE_EQ(repeat->value(), expected.u_repeat);
  EXPECT_EQ(repeat->locale().decimalPoint(), QStringLiteral("."));
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, expected);

  header->click();
  ASSERT_TRUE(editor.selection().clear().ok());
  panel->refresh();
  EXPECT_TRUE(panel->has_active_target());
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(apply->isEnabled());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 2, 3).ok());
  panel->refresh();
  EXPECT_FALSE(panel->expanded());
  EXPECT_TRUE(content->isHidden());
  EXPECT_TRUE(apply->isEnabled());
  cancel->click();
  panel->refresh();
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, expected);
  const auto restored = editor.modeling().mesh(created.value()).compiled_mesh();
  ASSERT_TRUE(restored.has_value());
  expect_same_texture_mesh(*restored, *original);
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  panel->refresh();
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, expected);
  EXPECT_TRUE(panel->expanded());
  panel->unbind();
}

TEST(QtToolSettingsPanelTests,
     UvPeelApplyFlushesAllStagedOptionsAndMatchesPublicApiWithOneUndo) {
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
      ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
      ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(expected_created.ok());
      ASSERT_TRUE(expected_editor.selection().select_object(expected_created.value()).ok());
      ASSERT_TRUE(expected_editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      const auto before = editor.modeling().mesh(created.value()).compiled_mesh();
      ASSERT_TRUE(before.has_value());
      const auto history_before = editor.history().snapshot();
      // Activation must expose editable settings before a guide exists.
      ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
      quader::QtToolSettingsPanel panel;
      panel.set_expanded(true);
      panel.setAttribute(Qt::WA_DontShowOnScreen, true);
      panel.bind(editor);
      panel.show();
      auto *axis = panel.findChild<QComboBox *>(
          QStringLiteral("tool_settings.control.uv_peel.axis"));
      auto *world = panel.findChild<QCheckBox *>(
          QStringLiteral("tool_settings.control.uv_peel.world_space"));
      auto *apply = panel.findChild<QPushButton *>(
          QStringLiteral("tool_settings.apply"));
      auto *cancel = panel.findChild<QPushButton *>(
          QStringLiteral("tool_settings.cancel"));
      ASSERT_NE(axis, nullptr);
      ASSERT_NE(world, nullptr);
      ASSERT_NE(apply, nullptr);
      ASSERT_NE(cancel, nullptr);
      EXPECT_FALSE(apply->isEnabled());
      EXPECT_TRUE(cancel->isEnabled());
      ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
      ASSERT_TRUE(expected_editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
      ASSERT_TRUE(expected_editor.selection().select_edge(expected_created.value(), 1, 2).ok());
      panel.refresh();
      ASSERT_TRUE(apply->isEnabled());
      axis->setCurrentIndex(selected_axis == quader::QuaderUvPeelAxis::U ? 0 : 1);
      world->setChecked(world_space);
      QCoreApplication::processEvents();
      constexpr std::array<const char *, 4> kFields{
          "u_repeat", "v_repeat", "u_offset", "v_offset"};
      constexpr std::array<double, 4> kValues{1.75, 2.25, 0.3, -0.2};
      for (std::size_t index = 0; index < kFields.size(); ++index) {
        auto *spin = panel.findChild<QDoubleSpinBox *>(
            QStringLiteral("tool_settings.control.uv_peel.%1").arg(
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
      EXPECT_EQ(editor.tools().active_tool(), "select");
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
      EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
      EXPECT_EQ(editor.modeling().uv_peel_settings().options, options);
      panel.unbind();
    }
  }
}

TEST(QtToolSettingsPanelTests,
     UvPeelInvalidDraftCannotApplyAndCancelDiscardsPendingText) {
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
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  const auto original = editor.modeling().mesh(created.value()).compiled_mesh();
  ASSERT_TRUE(original.has_value());
  const auto history_before = editor.history().snapshot();
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  auto *apply = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.apply"));
  auto *cancel = panel.findChild<QPushButton *>(
      QStringLiteral("tool_settings.cancel"));
  ASSERT_NE(repeat, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(cancel, nullptr);
  const auto preview = editor.modeling().mesh(created.value()).compiled_mesh();
  ASSERT_TRUE(preview.has_value());
  for (const QString &invalid : {QStringLiteral("0"), QStringLiteral("-")}) {
    type_spin_text(*repeat, invalid);
    apply->click();
    process_events_for(75);
    EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
    EXPECT_EQ(editor.modeling().uv_peel_settings().options,
              quader::QuaderUvPeelOptions{});
    EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
    const auto current = editor.modeling().mesh(created.value()).compiled_mesh();
    ASSERT_TRUE(current.has_value());
    expect_same_texture_mesh(*current, *preview);
    auto *text = repeat->findChild<QLineEdit *>();
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(text->text(), invalid);
  }
  type_spin_text(*repeat, QStringLiteral("2.5"));
  apply->click();
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 2.5);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  panel.refresh();
  repeat = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  ASSERT_NE(repeat, nullptr);
  repeat->setFocus(Qt::OtherFocusReason);
  repeat->setValue(7.0);
  ASSERT_TRUE(panel.numeric_interaction_active());
  // Press invalidates the queued edit before focus-out can flush it.
  send_mouse_event(*cancel, QEvent::MouseButtonPress, cancel->rect().center(),
                   Qt::LeftButton, Qt::LeftButton);
  EXPECT_FALSE(panel.numeric_interaction_active());
  send_mouse_event(*cancel, QEvent::MouseButtonRelease, cancel->rect().center(),
                   Qt::LeftButton, Qt::NoButton);
  process_events_for(75);
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 2.5);
  const auto restored = editor.modeling().mesh(created.value()).compiled_mesh();
  ASSERT_TRUE(restored.has_value());
  expect_same_texture_mesh(*restored, *original);
  EXPECT_EQ(editor.history().snapshot().undo_label, history_before.undo_label);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     UvPeelToolSwitchAndRuntimeRebindDiscardQueuedNumericEdits) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-uv-peel-runtime-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto find_repeat = [&]() {
    return panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("tool_settings.control.uv_peel.u_repeat"));
  };
  auto *repeat = find_repeat();
  ASSERT_NE(repeat, nullptr);
  repeat->setValue(2.0);
  finish_spin_edit(*repeat);
  EXPECT_DOUBLE_EQ(editor.modeling().uv_peel_settings().options.u_repeat, 2.0);
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  panel.refresh();
  process_events_for(75);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options,
            quader::QuaderUvPeelOptions{});
  repeat = find_repeat();
  ASSERT_NE(repeat, nullptr);
  repeat->setValue(8.0);
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  process_events_for(75);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options,
            quader::QuaderUvPeelOptions{});
  EXPECT_FALSE(panel.numeric_interaction_active());

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  panel.refresh();
  repeat = find_repeat();
  ASSERT_NE(repeat, nullptr);
  repeat->setValue(6.0);
  auto replacement = quader::create_standalone_editor();
  auto &replacement_editor = replacement.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(replacement_editor));
  quader::QuaderUvPeelOptions replacement_options;
  replacement_options.u_repeat = 9.0;
  ASSERT_TRUE(replacement_editor.modeling()
                  .set_uv_peel_options(replacement_options).ok());
  ASSERT_TRUE(replacement_editor.tools()
                  .activate(quader::ViewportToolId::UvPeel).ok());
  panel.bind(replacement_editor);
  process_events_for(75);
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options,
            quader::QuaderUvPeelOptions{});
  EXPECT_EQ(replacement_editor.modeling().uv_peel_settings().options,
            replacement_options);
  repeat = find_repeat();
  ASSERT_NE(repeat, nullptr);
  EXPECT_DOUBLE_EQ(repeat->value(), 9.0);
  repeat->setValue(10.0);
  panel.unbind();
  process_events_for(75);
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(replacement_editor.modeling().uv_peel_settings().options,
            replacement_options);
}

TEST(QtFaceTextureControlsTests,
     FaceTextureRotationUsesAbsoluteAnglesAndCoalescesOneNumericGesture) {
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
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::Rotate, 30.0).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 2).ok());
  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::Rotate, -15.0).ok());
  ASSERT_TRUE(editor.selection().select_faces(source.value(), {1, 2}).ok());

  quader::QtFaceTextureControls panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *rotate = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.rotate"));
  auto *scale_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  ASSERT_NE(rotate, nullptr);
  ASSERT_NE(scale_x, nullptr);
  EXPECT_EQ(rotate->text(), QStringLiteral("--"));

  rotate->setValue(10.0);
  ASSERT_TRUE(process_events_until([&] {
    return std::abs(editor.modeling().face_texture_settings().rotation_degrees - 10.0) < 1.0e-4;
  }, 1000));
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees, 10.0, 1.0e-4);
  rotate->setValue(20.0);
  ASSERT_TRUE(process_events_until([&] {
    return std::abs(editor.modeling().face_texture_settings().rotation_degrees - 20.0) < 1.0e-4;
  }, 1000));
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees, 20.0, 1.0e-4);
  finish_spin_edit(*rotate);
  EXPECT_FALSE(panel.numeric_interaction_active());
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_EQ(rotate->text(), QStringLiteral("--"));
  ASSERT_TRUE(editor.history().redo().ok());
  panel.refresh();

  // A subsequent gesture is separate; one undo cannot consume both gestures.
  scale_x->setValue(2.0);
  finish_spin_edit(*scale_x);
  scale_x->setValue(3.0);
  finish_spin_edit(*scale_x);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_DOUBLE_EQ(scale_x->value(), 2.0);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_DOUBLE_EQ(scale_x->value(), 1.0);

  ASSERT_TRUE(editor.selection().select_face(
                  source.value(), 3, quader::QuaderSelectionEdit::Add).ok());
  panel.refresh();
  EXPECT_EQ(rotate->text(), QStringLiteral("--"));
  rotate->setValue(8.0);
  finish_spin_edit(*rotate);
  ASSERT_TRUE(editor.selection().select_face(
                  source.value(), 3, quader::QuaderSelectionEdit::Remove).ok());
  panel.refresh();
  EXPECT_NEAR(rotate->value(), 8.0, 1.0e-4);
  panel.unbind();
}

TEST(QtFaceTextureControlsTests,
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
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  quader::QtFaceTextureControls panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  auto *scale_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.scale_x"));
  auto *shift_x = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("face_texture.texture.shift_x"));
  auto *reason = panel.findChild<QLabel *>(
      QStringLiteral("face_texture.texture.disabled_reason"));
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
  panel.refresh();
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
  panel.refresh();
  EXPECT_FALSE(scale_x->isEnabled());
  EXPECT_FALSE(reason->isHidden());
  EXPECT_FALSE(reason->text().isEmpty());
  ASSERT_TRUE(editor.modeling().cancel_transform_preview().ok());
  panel.refresh();
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
  ASSERT_TRUE(replacement_editor.selection().select_object(replacement_source.value()).ok());
  ASSERT_TRUE(replacement_editor.mode().set(quader::QuaderEditorMode::Edit).ok());
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

TEST(QtFaceTextureControlsTests,
     AbandonRuntimeBindingDiscardsPendingEditsAfterOwnerDestruction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-face-texture-abandon-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  quader::QtFaceTextureControls panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  {
    auto outgoing = quader::create_standalone_editor();
    auto &editor = outgoing.editor();
    const auto source = create_texture_action_surface(editor);
    ASSERT_TRUE(source.ok());
    ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
    panel.bind(editor);
    auto *scale = panel.findChild<QDoubleSpinBox *>(
        QStringLiteral("face_texture.texture.scale_x"));
    ASSERT_NE(scale, nullptr);
    scale->setValue(9.0);
    ASSERT_TRUE(panel.numeric_interaction_active());
  }
  // Runtime installation abandons callbacks after the old owners are gone.
  panel.abandon_runtime_binding();
  EXPECT_FALSE(panel.numeric_interaction_active());
  auto *section = panel.findChild<QWidget *>(
      QStringLiteral("face_texture.section.texture"));
  ASSERT_NE(section, nullptr);
  EXPECT_TRUE(section->isHidden());

  auto replacement = quader::create_standalone_editor();
  auto &editor = replacement.editor();
  const auto source = create_texture_action_surface(editor);
  ASSERT_TRUE(source.ok());
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(source.value(), 1).ok());
  panel.bind(editor);
  const auto revision = editor.revisions();
  process_events_for(75);
  EXPECT_FALSE(panel.numeric_interaction_active());
  EXPECT_EQ(editor.modeling().face_texture_settings().scale_x,
            std::optional<double>(1.0));
  EXPECT_EQ(editor.revisions().modeling_content_revision,
            revision.modeling_content_revision);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     FootprintKeepsRetainedSettingsControlsWithoutAResetAction) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-footprint-controls-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(
      editor.tools().activate(quader::ViewportToolId::CreatePrimitive).ok());

  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *subdivisions = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.box.subdivisions_x"));
  ASSERT_NE(subdivisions, nullptr);
  EXPECT_EQ(panel.findChild<QPushButton *>(
                QStringLiteral("tool_settings.reset_defaults")), nullptr);
  QPointer<QSpinBox> retained_subdivisions(subdivisions);

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
  EXPECT_EQ(panel.findChild<QPushButton *>(
                QStringLiteral("tool_settings.reset_defaults")), nullptr);

  const quader::ModelingCommandResult canceled =
      session.modeling().cancel_box();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  panel.refresh();
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.box.subdivisions_x")),
            retained_subdivisions.data());
  EXPECT_EQ(panel.findChild<QPushButton *>(
                QStringLiteral("tool_settings.reset_defaults")), nullptr);
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
  panel.set_expanded(true);
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
  auto *closed_label = panel.findChild<QLabel *>(QStringLiteral(
      "tool_settings.control.curve.closed.label"));
  ASSERT_NE(closed_label, nullptr);
  EXPECT_TRUE(closed->text().isEmpty());
  EXPECT_EQ(closed_label->text(), QStringLiteral("Chiusa"));
  EXPECT_EQ(closed->accessibleName(), closed_label->text());
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
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  auto *settings_section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.scroll"));
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
  ASSERT_NE(settings_section, nullptr);
  ASSERT_NE(solidify, nullptr);
  ASSERT_NE(resolution_row, nullptr);
  ASSERT_NE(resolution, nullptr);
  ASSERT_NE(radius_row, nullptr);
  ASSERT_NE(radius, nullptr);
  ASSERT_NE(apply_solidify, nullptr);
  EXPECT_TRUE(panel.has_active_target());
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
  panel.set_expanded(true);
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
  panel.set_expanded(true);
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
                QStringLiteral("numeric_spin_box.increase"))
          : nullptr;
  auto *radius_spin = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.curve.solidify.radius"));
  ASSERT_NE(radius_spin, nullptr);
  auto *radius_editor = radius_spin->findChild<QLineEdit *>();
  ASSERT_NE(radius_editor, nullptr);
  ASSERT_NE(solidify, nullptr);
  ASSERT_NE(resolution_spinner, nullptr);
  ASSERT_NE(resolution_editor, nullptr);
  ASSERT_NE(resolution_increase, nullptr);
  ASSERT_TRUE(panel.findChildren<QSlider *>().isEmpty());
  solidify->click();
  panel.refresh();
  ASSERT_TRUE(resolution_spinner->isEnabled());
  ASSERT_TRUE(radius_spin->isEnabled());

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
  const QPoint resolution_first(resolution_crossed.x() + 2,
                                resolution_crossed.y());
  const QPoint resolution_second(resolution_crossed.x() + 6,
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
  const double radius_before_drag = owner_number("curve.solidify.radius");
  const QPoint radius_crossed = begin_numeric_scrub(*radius_editor);
  ASSERT_TRUE(quader::qt_install_numeric_spin_box(*radius_spin).is_dragging());
  const QPoint radius_first = radius_crossed + QPoint(2, 0);
  const QPoint radius_second = radius_crossed + QPoint(4, 0);
  send_mouse_event(*radius_editor, QEvent::MouseMove, radius_first,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(process_events_until([&] {
    return owner_number("curve.solidify.radius") != radius_before_drag;
  }, 1000));
  const double first_radius = owner_number("curve.solidify.radius");
  EXPECT_NE(first_radius, radius_before_drag);
  send_mouse_event(*radius_editor, QEvent::MouseMove, radius_second,
                   Qt::NoButton, Qt::LeftButton);
  ASSERT_TRUE(process_events_until(
      [&] { return owner_number("curve.solidify.radius") != first_radius; },
      1000));
  const double final_radius = owner_number("curve.solidify.radius");
  EXPECT_NE(final_radius, first_radius);
  send_mouse_event(*radius_editor, QEvent::MouseButtonRelease, radius_second,
                   Qt::LeftButton, Qt::NoButton);
  ASSERT_TRUE(editor.history().undo().ok());
  panel.refresh();
  EXPECT_NEAR(owner_number("curve.solidify.radius"), 0.1, 1.0e-6);
  EXPECT_FALSE(session.history().can_undo())
      << "periodic Radius previews must merge into one gesture";

  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     NativeOverlayHeaderAndComboPopupOwnInputOnlyWithinTheirSurface) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-settings-native-window-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_NO_FATAL_FAILURE(prepare_empty_component_context(editor));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  QMainWindow main_window;
  main_window.setAttribute(Qt::WA_DontShowOnScreen, true);
  main_window.resize(900, 600);
  auto *central = new QWidget(&main_window);
  main_window.setCentralWidget(central);
  auto *panel = new quader::QtToolSettingsPanel(central);
  panel->setAttribute(Qt::WA_NativeWindow, true);
  panel->bind(editor);
  panel->set_expanded(true);
  panel->setGeometry(6, 100, 280, 450);
  panel->show();
  main_window.show();
  QCoreApplication::processEvents();
  auto *header = panel->findChild<QToolButton *>(
      QStringLiteral("tool_settings.target_title"));
  auto *combo = panel->findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.uv_peel.axis"));
  ASSERT_NE(header, nullptr);
  ASSERT_NE(combo, nullptr);
  ASSERT_NE(panel->windowHandle(), nullptr);
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(header));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(combo));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(panel->windowHandle()));
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(main_window.windowHandle()));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(
      main_window.windowHandle(), header->mapToGlobal(header->rect().center())));
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(
      main_window.windowHandle(), central->mapToGlobal(QPoint(800, 100))));
  combo->showPopup();
  QCoreApplication::processEvents();
  QWidget *popup = combo->view()->window();
  ASSERT_NE(popup, nullptr);
  ASSERT_NE(popup->windowHandle(), nullptr);
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(combo->view()));
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(popup->windowHandle()));
  combo->hidePopup();
  panel->set_expanded(false);
  EXPECT_TRUE(quader::qt_event_target_belongs_to_tool_settings(header));
  panel->hide();
  QCoreApplication::processEvents();
  EXPECT_FALSE(quader::qt_event_target_belongs_to_tool_settings(
      main_window.windowHandle(), header->mapToGlobal(header->rect().center())));
  panel->unbind();
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
  window.set_expanded(true);
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
  EXPECT_EQ(window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.type")), nullptr);
  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateCreateCone).ok());
  window.refresh();
  QCoreApplication::processEvents();
  EXPECT_EQ(editor.tools().active_primitive_type(), "cone");
  ASSERT_NE(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.primitive.cone.side_count")),
            nullptr);

  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateCreateTorus).ok());
  window.refresh();
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

  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateCreateCylinder).ok());
  window.refresh();
  QCoreApplication::processEvents();
  EXPECT_EQ(editor.tools().active_primitive_type(), "cylinder");

  auto *sides = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.cylinder.side_count"));
  ASSERT_NE(sides, nullptr);
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(tool_settings_primitive_preview(),
                                  "Focused Settings Cylinder")
                  .ok);
  auto *sides_editor = sides->findChild<QLineEdit *>();
  ASSERT_NE(sides_editor, nullptr);
  sides_editor->setText(QStringLiteral("7"));
  sides_editor->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_event(*sides_editor, Qt::Key_Return);
  EXPECT_EQ(session.primitive_creation().cylinder_settings().side_count, 7);
  EXPECT_EQ(editor.tools().active_primitive_type(), "cylinder");

  // Sphere's topology choice remains a native combo after removing the shape
  // picker. Its Return/Escape handling must keep the same editing semantics.
  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateCreateSphere).ok());
  ASSERT_TRUE(session.primitive_creation()
                  .accept_preview(tool_settings_primitive_preview(),
                                  "Focused Settings Sphere")
                  .ok);
  window.refresh();
  QCoreApplication::processEvents();
  auto *sphere_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.sphere.type"));
  ASSERT_NE(sphere_type, nullptr);
  ASSERT_EQ(sphere_type->count(), 2);
  EXPECT_EQ(sphere_type->currentData().toString(), QStringLiteral("uv"));
  sphere_type->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_event(*sphere_type, Qt::Key_Return);

  EXPECT_EQ(editor.tools().active_tool(), "create_primitive");
  EXPECT_TRUE(editor.history().can_undo());

  active_tool = quader::ViewportToolId::CreatePrimitive;
  ASSERT_TRUE(editor.commands().execute(
      quader::quader_command::kToolsActivateCreateSphere).ok());
  window.refresh();
  QCoreApplication::processEvents();
  sphere_type = window.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.sphere.type"));
  ASSERT_NE(sphere_type, nullptr);
  EXPECT_EQ(sphere_type->currentData().toString(), QStringLiteral("uv"));
  sphere_type->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  send_key_event(*sphere_type, Qt::Key_Escape);

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

TEST(QtToolSettingsPanelTests,
     SphereTypeControlsRetainIndependentValuesAndResetToUvDefaults) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-sphere-settings-types-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(
      quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "create_primitive", "primitive.type", std::string_view("sphere")).ok());
  ASSERT_TRUE(session.primitive_creation().accept_preview(
      tool_settings_primitive_preview(), "Sphere Settings").ok);
  const auto defaults = session.primitive_creation().sphere_settings();

  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  EXPECT_TRUE(panel.expanded());
  panel.set_expanded(false);
  const QSize collapsed_size = panel.sizeHint();
  panel.set_expanded(true);
  panel.resize(panel.sizeHint());
  QCoreApplication::processEvents();
  auto *type = panel.findChild<QComboBox *>(
      QStringLiteral("tool_settings.control.primitive.sphere.type"));
  auto *radial = panel.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.primitive.sphere.radial_segments"));
  auto *rings = panel.findChild<QSpinBox *>(
      QStringLiteral("tool_settings.control.primitive.sphere.rings"));
  ASSERT_NE(type, nullptr);
  ASSERT_NE(radial, nullptr);
  ASSERT_NE(rings, nullptr);
  EXPECT_EQ(type->accessibleName(), QStringLiteral("Type"));
  ASSERT_EQ(type->count(), 2);
  EXPECT_EQ(type->currentData().toString(), QStringLiteral("uv"));
  EXPECT_EQ(type->itemText(type->findData(QStringLiteral("uv"))),
            QStringLiteral("UV Sphere"));
  EXPECT_EQ(type->itemText(type->findData(QStringLiteral("quad"))),
            QStringLiteral("Quad Sphere"));
  EXPECT_TRUE(type->isVisible());
  EXPECT_TRUE(radial->isVisible());
  EXPECT_TRUE(rings->isVisible());
  EXPECT_LT(type->mapTo(&panel, QPoint()).y(),
            radial->mapTo(&panel, QPoint()).y());
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.subdivision")), nullptr);
  radial->setValue(20);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      radial, "editingFinished", Qt::DirectConnection));
  rings->setValue(9);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      rings, "editingFinished", Qt::DirectConnection));
  EXPECT_EQ(session.primitive_creation().sphere_settings().radial_segments, 20);
  EXPECT_EQ(session.primitive_creation().sphere_settings().rings, 9);
  type->setCurrentIndex(type->findData(QStringLiteral("quad")));
  QCoreApplication::processEvents();
  EXPECT_TRUE(panel.expanded());
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.radial_segments")), nullptr);
  EXPECT_EQ(panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.rings")), nullptr);
  auto *subdivision = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.subdivision"));
  ASSERT_NE(subdivision, nullptr);
  EXPECT_TRUE(subdivision->isVisible());
  EXPECT_EQ(subdivision->accessibleName(), QStringLiteral("Subdivision"));
  EXPECT_EQ(subdivision->minimum(), 1);
  EXPECT_EQ(subdivision->maximum(), 6);
  EXPECT_EQ(subdivision->value(), 3);
  for (const char *id : {"primitive.sphere.hemisphere", "primitive.flip_faces"}) {
    auto *toggle = panel.findChild<QCheckBox *>(
        QStringLiteral("tool_settings.control.") + QString::fromLatin1(id));
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isVisible());
    EXPECT_TRUE(toggle->isEnabled());
  }
  subdivision->setValue(4);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      subdivision, "editingFinished", Qt::DirectConnection));
  EXPECT_EQ(session.primitive_creation().sphere_settings().subdivision, 4);

  panel.set_expanded(false);
  ASSERT_TRUE(editor.tools().set_setting(
      "create_primitive", "primitive.sphere.type", std::string_view("uv")).ok());
  panel.refresh();
  EXPECT_FALSE(panel.expanded());
  EXPECT_EQ(panel.sizeHint(), collapsed_size);
  panel.set_expanded(true);
  radial = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.radial_segments"));
  rings = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.rings"));
  ASSERT_NE(radial, nullptr);
  ASSERT_NE(rings, nullptr);
  EXPECT_EQ(radial->value(), 20);
  EXPECT_EQ(rings->value(), 9);
  type = panel.findChild<QComboBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.type"));
  ASSERT_NE(type, nullptr);
  type->setCurrentIndex(type->findData(QStringLiteral("quad")));
  QCoreApplication::processEvents();
  subdivision = panel.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.subdivision"));
  ASSERT_NE(subdivision, nullptr);
  EXPECT_EQ(subdivision->value(), 4);
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  panel.refresh();
  QCoreApplication::processEvents();
  type = panel.findChild<QComboBox *>(QStringLiteral(
      "tool_settings.control.primitive.sphere.type"));
  ASSERT_NE(type, nullptr);
  EXPECT_EQ(type->currentData().toString(), QStringLiteral("uv"));
  const auto reset_settings = session.primitive_creation().sphere_settings();
  EXPECT_EQ(reset_settings.radial_segments, defaults.radial_segments);
  EXPECT_EQ(reset_settings.rings, defaults.rings);
  EXPECT_EQ(reset_settings.subdivision, 3);
  EXPECT_EQ(reset_settings.type, defaults.type);
  EXPECT_TRUE(panel.expanded());
  EXPECT_FALSE(panel.numeric_interaction_active());
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     SphereTypeChangeDiscardsPendingEditsFromReplacedTopologyControls) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-sphere-stale-settings-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.tools().activate(
      quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "create_primitive", "primitive.type", std::string_view("sphere")).ok());
  ASSERT_TRUE(session.primitive_creation().accept_preview(
      tool_settings_primitive_preview(), "Sphere Pending Settings").ok);
  const auto defaults = session.primitive_creation().sphere_settings();
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_expanded(true);
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();

  for (const auto &[field, next_type, pending] : {
           std::tuple{"radial_segments", "quad", 25},
           std::tuple{"subdivision", "uv", 5}}) {
    SCOPED_TRACE(field);
    auto *spin = panel.findChild<QSpinBox *>(
        QStringLiteral("tool_settings.control.primitive.sphere.") +
        QString::fromLatin1(field));
    ASSERT_NE(spin, nullptr);
    const QPointer<QSpinBox> outgoing(spin);
    spin->setValue(pending);
    ASSERT_TRUE(panel.numeric_interaction_active());
    ASSERT_TRUE(editor.tools().set_setting("create_primitive",
        "primitive.sphere.type", std::string_view(next_type)).ok());
    panel.refresh();
    EXPECT_TRUE(outgoing.isNull());
    EXPECT_FALSE(panel.numeric_interaction_active());
    const auto settings_revision = editor.tools().active_settings_revision();
    const auto content_revision = editor.revisions().modeling_content_revision;
    process_events_for(75);
    EXPECT_EQ(session.primitive_creation().sphere_settings().radial_segments,
              defaults.radial_segments);
    EXPECT_EQ(session.primitive_creation().sphere_settings().subdivision,
              defaults.subdivision);
    EXPECT_EQ(editor.tools().active_settings_revision(), settings_revision);
    EXPECT_EQ(editor.revisions().modeling_content_revision, content_revision);
  }
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     SphereTopologyControlsLocalizeFitScaledPanelsAndReuseUnchangedPresentation) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-sphere-settings-presentation-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(
      QApplication::palette());
  quader::QtUiScaleApplicationController scale_controller(application);
  const auto settle_layout = [] {
    auto *dispatcher = QAbstractEventDispatcher::instance();
    if (dispatcher == nullptr) {
      return false;
    }
    for (int iteration = 0; iteration < 64; ++iteration) {
      if (!dispatcher->processEvents(QEventLoop::AllEvents)) {
        return true;
      }
    }
    return false;
  };
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  ASSERT_TRUE(editor.tools().activate(
      quader::ViewportToolId::CreatePrimitive).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "create_primitive", "primitive.type", std::string_view("sphere")).ok());
  quader::QtToolSettingsPanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.set_expanded(true);
  panel.bind(editor);
  panel.show();
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  const QString screenshot_directory =
      qEnvironmentVariable("QUADER_SPHERE_SETTINGS_SCREENSHOT_DIR");
  if (!screenshot_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(screenshot_directory));
  }
  for (const char *locale : {"en-US", "it-IT"}) {
    SCOPED_TRACE(locale);
    const bool italian = std::string_view(locale) == "it-IT";
    ASSERT_TRUE(localization.select_locale(locale));
    panel.set_localization(localization);
    for (const char *sphere_type : {"uv", "quad"}) {
      SCOPED_TRACE(sphere_type);
      ASSERT_TRUE(editor.tools().set_setting("create_primitive",
          "primitive.sphere.type", std::string_view(sphere_type)).ok());
      panel.refresh();
      auto *type = panel.findChild<QComboBox *>(QStringLiteral(
          "tool_settings.control.primitive.sphere.type"));
      ASSERT_NE(type, nullptr);
      EXPECT_EQ(type->accessibleName(),
                italian ? QStringLiteral("Tipo") : QStringLiteral("Type"));
      EXPECT_EQ(type->itemText(type->findData(QStringLiteral("uv"))),
                italian ? QStringLiteral("Sfera UV") : QStringLiteral("UV Sphere"));
      EXPECT_EQ(type->itemText(type->findData(QStringLiteral("quad"))),
                italian ? QStringLiteral("Sfera Quad") : QStringLiteral("Quad Sphere"));
      if (std::string_view(sphere_type) == "quad") {
        auto *subdivision = panel.findChild<QSpinBox *>(QStringLiteral(
            "tool_settings.control.primitive.sphere.subdivision"));
        ASSERT_NE(subdivision, nullptr);
        EXPECT_EQ(subdivision->accessibleName(), italian
            ? QStringLiteral("Suddivisione") : QStringLiteral("Subdivision"));
      }
      auto *scroll = panel.findChild<QScrollArea *>(
          QStringLiteral("tool_settings.scroll"));
      auto *actions = panel.findChild<QWidget *>(
          QStringLiteral("tool_settings.actions"));
      ASSERT_NE(scroll, nullptr);
      ASSERT_NE(actions, nullptr);
      for (float scale : {1.0F, 1.5F, 2.0F}) {
        SCOPED_TRACE(scale);
        scale_controller.apply_factor(scale);
        panel.apply_ui_scale(scale_controller.metrics());
        // Font and style transitions post nested layout requests. Let those
        // reach the retained rows before the normal presentation refresh and
        // viewport-host resize measure the new preferred size.
        ASSERT_TRUE(settle_layout());
        panel.refresh();
        panel.resize(panel.sizeHint());
        ASSERT_TRUE(settle_layout());
        const QFont expected_font = scale_controller.metrics().font(
            scale_controller.baseline_font());
        EXPECT_EQ(type->font().pointSizeF(), expected_font.pointSizeF());
        EXPECT_EQ(type->font().pixelSize(), expected_font.pixelSize());
        EXPECT_TRUE(panel.expanded());
        EXPECT_EQ(scroll->verticalScrollBar()->maximum(), 0);
        EXPECT_TRUE(panel.rect().contains(actions->geometry()));
        EXPECT_GE(type->height(), type->minimumSizeHint().height());
        for (auto *spin : panel.findChildren<QSpinBox *>()) {
          ASSERT_TRUE(spin->isVisible());
          EXPECT_GE(spin->height(), spin->minimumSizeHint().height());
          const QRect bounds(spin->mapTo(scroll->viewport(), QPoint()), spin->size());
          EXPECT_TRUE(scroll->viewport()->rect().contains(bounds));
        }
        for (auto *label : panel.findChildren<QLabel *>()) {
          if (!label->objectName().endsWith(QStringLiteral(".label"))) {
            continue;
          }
          SCOPED_TRACE(label->objectName().toStdString());
          ASSERT_TRUE(label->isVisible());
          EXPECT_GT(label->width(), 0);
          EXPECT_GE(label->height(), label->heightForWidth(label->width()));
          const QRect bounds(label->mapTo(scroll->viewport(), QPoint()), label->size());
          EXPECT_TRUE(scroll->viewport()->rect().contains(bounds));
        }
        if (!screenshot_directory.isEmpty()) {
          QImage screenshot(panel.size(), QImage::Format_ARGB32_Premultiplied);
          screenshot.fill(Qt::transparent);
          panel.render(&screenshot);
          EXPECT_TRUE(screenshot.save(QDir(screenshot_directory).filePath(
              QStringLiteral("sphere-%1-%2-%3.png")
                  .arg(QString::fromLatin1(sphere_type), QString::fromLatin1(locale))
                  .arg(static_cast<int>(std::lround(scale * 100.0F))))));
        }
      }
      panel.refresh();
      ASSERT_TRUE(settle_layout());
      TexturePresentationEvents events;
      const auto widgets = panel.findChildren<QWidget *>();
      for (QWidget *widget : widgets) {
        widget->installEventFilter(&events);
      }
      const QPointer<QComboBox> retained_type(type);
      int changes = 0;
      panel.presentation_changed = [&] { ++changes; };
      const auto settings_revision = editor.tools().active_settings_revision();
      const auto content_revision = editor.revisions().modeling_content_revision;
      for (int frame = 0; frame < 30; ++frame) {
        panel.refresh();
      }
      EXPECT_EQ(changes, 0);
      EXPECT_EQ(events.changes, 0);
      EXPECT_EQ(panel.findChild<QComboBox *>(QStringLiteral(
          "tool_settings.control.primitive.sphere.type")), retained_type.data());
      EXPECT_EQ(editor.tools().active_settings_revision(), settings_revision);
      EXPECT_EQ(editor.revisions().modeling_content_revision, content_revision);
      panel.presentation_changed = {};
    }
  }
  panel.unbind();
}

TEST(QtToolSettingsNumberSliderTests,
     PrimitiveSubdivisionPointerDragPublishesBoundedLiveGeometry) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-primitive-numeric-preview-tests";
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
                                  "Live Numeric Box")
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
  window.set_expanded(true);
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.bind(editor);
  window.show();
  window.activateWindow();
  QCoreApplication::processEvents();

  auto *subdivisions_spin = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.primitive.box.subdivisions_x"));
  ASSERT_NE(subdivisions_spin, nullptr);
  ASSERT_NE(dynamic_cast<quader::QtSpinBox *>(subdivisions_spin), nullptr);
  ASSERT_TRUE(window.findChildren<QSlider *>().isEmpty());
  auto *subdivisions_editor = subdivisions_spin->findChild<QLineEdit *>();
  ASSERT_NE(subdivisions_editor, nullptr);
  auto &subdivisions_controller =
      quader::qt_install_numeric_spin_box(*subdivisions_spin);
  const QPoint crossed = begin_numeric_scrub(*subdivisions_editor);
  ASSERT_TRUE(subdivisions_controller.is_dragging());
  const std::array<QPoint, 5> moves{
      crossed + QPoint(2, 0), crossed + QPoint(4, 0),
      crossed + QPoint(6, 0), crossed + QPoint(8, 0),
      crossed + QPoint(10, 0),
  };

  const quader::QuaderRevisionStamp revisions_before = editor.revisions();
  const std::uint64_t settings_revision_before =
      editor.tools().active_settings_revision();
  const quader::ModelingSdkProjectionStats projection_before =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  for (std::size_t index = 0; index < 3; ++index) {
    send_mouse_event(*subdivisions_editor, QEvent::MouseMove, moves[index],
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
  // fixed preview cadence while the numeric scrub remains held.
  ASSERT_TRUE(process_events_until([&] {
    return session.primitive_creation().box_settings().subdivisions_x ==
           first_pending_subdivisions;
  }, 1000));
  ASSERT_TRUE(subdivisions_controller.is_dragging());
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

  // An idle held numeric scrub performs no further owner, projection, or geometry
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
  // numeric value once, then leave no delayed publication behind.
  for (std::size_t index = 3; index < moves.size(); ++index) {
    send_mouse_event(*subdivisions_editor, QEvent::MouseMove, moves[index],
                     Qt::NoButton, Qt::LeftButton);
  }
  const int final_pending_subdivisions = subdivisions_spin->value();
  ASSERT_GT(final_pending_subdivisions, first_pending_subdivisions);
  EXPECT_EQ(session.primitive_creation().box_settings().subdivisions_x,
            first_pending_subdivisions);
  send_mouse_event(*subdivisions_editor, QEvent::MouseButtonRelease,
                   moves.back(), Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(subdivisions_controller.is_dragging());
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

TEST(QtToolSettingsPanelTests,
     BevelSaturatedWidthIsHonestRetainedAndImmediatelyReversible) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-bevel-saturated-width-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  // Exercise a limit known at activation, then discovered through text and
  // through an in-flight scrub on an already retained settings panel.
  for (const int discovery : {0, 1, 2}) {
    SCOPED_TRACE(discovery);
    quader::SceneSession session;
    quader::QuaderEditorApi editor(
        quader::QuaderApiSessionInternalAccess::from_scene_session(session));
    const auto source = editor.modeling().create_box(
        {.min = {-0.25F, -0.125F, -0.25F},
         .max = {0.25F, 2.125F, 0.25F}},
        "Narrow Bevel Numeric Source");
    ASSERT_TRUE(source.ok()) << source.error_message();
    ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor.selection().select_edge(source.value(), 2, 3).ok());
    ASSERT_TRUE(editor.commands()
                    .execute(editor.commands().command_id_for_modeling_action(
                        "bevel_edges"))
                    .ok());

    if (discovery != 0) {
      ASSERT_TRUE(editor.tools().set_setting(
          "edge_bevel", "operation.edge_bevel.width", 0.1).ok());
      // A new geometry configuration discards the previous learned bound.
      ASSERT_TRUE(editor.tools().set_setting(
          "edge_bevel", "operation.edge_bevel.segments", 2.0).ok());
    }

    quader::QtToolSettingsPanel panel;
    panel.set_expanded(true);
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    panel.activateWindow();
    QCoreApplication::processEvents();
    auto *width = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
        "tool_settings.control.operation.edge_bevel.width"));
    ASSERT_NE(width, nullptr);
    auto *line = width->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    const QPointer<QDoubleSpinBox> retained_width(width);
    const auto owner_width = [&editor] {
      const auto target = editor.tools().active_settings();
      const auto *setting = find_setting(target, "operation.edge_bevel.width");
      EXPECT_NE(setting, nullptr);
      return setting != nullptr ? setting->number_value : 0.0;
    };
    const auto maximum_hint = [&panel] {
      return panel.findChild<QLabel *>(QStringLiteral(
          "tool_settings.control.operation.edge_bevel.width.maximum"));
    };
    EXPECT_EQ(maximum_hint(), nullptr);
    EXPECT_TRUE(width->toolTip().isEmpty());
    EXPECT_TRUE(width->parentWidget()->toolTip().isEmpty());
    auto *profile = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
        "tool_settings.control.operation.edge_bevel.profile"));
    ASSERT_NE(profile, nullptr);
    EXPECT_TRUE(profile->isEnabled());
    EXPECT_TRUE(profile->toolTip().isEmpty());
    EXPECT_TRUE(profile->parentWidget()->toolTip().isEmpty());
    if (discovery != 0) {
      ASSERT_DOUBLE_EQ(width->value(), 0.1);
      EXPECT_GT(width->maximum(), 1.0);
    }
    if (discovery == 1) {
      type_spin_text(*width, QStringLiteral("1"));
      send_key_event(*line, Qt::Key_Return);
    } else if (discovery == 2) {
      const QPoint crossed = begin_numeric_scrub(*line);
      const QPoint overshoot = crossed + QPoint(100, 0);
      send_mouse_event(*line, QEvent::MouseMove, overshoot, Qt::NoButton,
                       Qt::LeftButton);
      ASSERT_GT(width->value(), 1.0);
      ASSERT_TRUE(process_events_until([&] {
        return width->maximum() < 1.0 && width->value() < 1.0;
      }, 1000));
      ASSERT_TRUE(quader::qt_install_numeric_spin_box(*width).is_dragging());
      const double saturated = width->value();
      const QPoint reversed = overshoot - QPoint(1, 0);
      send_mouse_event(*line, QEvent::MouseMove, reversed, Qt::NoButton,
                       Qt::LeftButton);
      EXPECT_NEAR(width->value(), saturated - 0.01, 1.0e-7)
          << "owner correction must discard the overshoot during a live scrub";
      send_mouse_event(*line, QEvent::MouseButtonRelease, reversed,
                       Qt::LeftButton, Qt::NoButton);
      EXPECT_NEAR(owner_width(), saturated - 0.01, 1.0e-7);
    }

    ASSERT_EQ(panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                  "tool_settings.control.operation.edge_bevel.width")),
              retained_width.data());
    const auto accepted = editor.tools().active_settings();
    const auto *accepted_width = find_setting(accepted, "operation.edge_bevel.width");
    ASSERT_NE(accepted_width, nullptr);
    const double maximum = accepted_width->max_value;
    EXPECT_NEAR(maximum, 0.4995, 1.0e-6);
    EXPECT_NEAR(width->maximum(), maximum, 1.0e-7);
    EXPECT_NEAR(width->value(), owner_width(), 1.0e-7);
    EXPECT_EQ(maximum_hint(), nullptr);
    EXPECT_TRUE(width->toolTip().isEmpty());
    if (discovery != 2) {
      EXPECT_NEAR(width->value(), maximum, 1.0e-7);
    }

    // Once known, the real geometry limit is also the native editor's bound.
    // A held pointer can travel beyond it, then reverse immediately by one step.
    width->setValue(maximum);
    finish_spin_edit(*width);
    const QPoint crossed = begin_numeric_scrub(*line);
    const QPoint overshoot = crossed + QPoint(100, 0);
    send_mouse_event(*line, QEvent::MouseMove, overshoot, Qt::NoButton,
                     Qt::LeftButton);
    EXPECT_NEAR(width->value(), maximum, 1.0e-7);
    const QPoint reversed = overshoot - QPoint(1, 0);
    send_mouse_event(*line, QEvent::MouseMove, reversed, Qt::NoButton,
                     Qt::LeftButton);
    EXPECT_NEAR(width->value(), maximum - 0.01, 1.0e-7);
    send_mouse_event(*line, QEvent::MouseButtonRelease, reversed,
                     Qt::LeftButton, Qt::NoButton);
    EXPECT_NEAR(owner_width(), maximum - 0.01, 1.0e-7);
    auto *decrease = width->findChild<QToolButton *>(
        QStringLiteral("numeric_spin_box.decrease"));
    ASSERT_NE(decrease, nullptr);
    decrease->click();
    EXPECT_NEAR(width->value(), maximum - 0.02, 1.0e-7);
    finish_spin_edit(*width);
    EXPECT_NEAR(owner_width(), maximum - 0.02, 1.0e-7);

    auto render_snapshot = quader::build_render_scene_snapshot(
        session.scene(), session.assets());
    ASSERT_EQ(render_snapshot.meshes.size(), 1u);
    const auto positions = [](const quader::RenderSceneSnapshot &snapshot) {
      std::vector<std::array<float, 3>> result;
      for (const auto &vertex : snapshot.meshes.front().mesh.vertices) {
        result.push_back({vertex.position.x, vertex.position.y, vertex.position.z});
      }
      return result;
    };
    const auto previous_positions = positions(render_snapshot);
    type_spin_text(*width, width->locale().toString(0.4, 'f', 1));
    send_key_event(*line, Qt::Key_Return);
    EXPECT_DOUBLE_EQ(owner_width(), 0.4);
    EXPECT_DOUBLE_EQ(width->value(), 0.4);
    EXPECT_FALSE(quader::render_scene_snapshot_matches_sources(
        render_snapshot, session.scene(), session.assets()));
    render_snapshot = quader::build_render_scene_snapshot(
        session.scene(), session.assets(), {.previous = &render_snapshot});
    ASSERT_EQ(render_snapshot.meshes.size(), 1u);
    EXPECT_TRUE(render_snapshot.meshes.front().mesh_dirty);
    EXPECT_NE(positions(render_snapshot), previous_positions);
    const auto unchanged = quader::build_render_scene_snapshot(
        session.scene(), session.assets(), {.previous = &render_snapshot});
    EXPECT_TRUE(unchanged.reused_previous_records);
    EXPECT_FALSE(unchanged.dirty.any_dirty());

    for (const double excessive : {0.7, 1.0}) {
      SCOPED_TRACE(excessive);
      type_spin_text(*width, width->locale().toString(excessive, 'f', 1));
      send_key_event(*line, Qt::Key_Return);
      EXPECT_LE(width->value(), maximum + 1.0e-7);
      EXPECT_LE(owner_width(), maximum + 1.0e-7);
      bool parsed = false;
      const double displayed = width->locale().toDouble(line->text(), &parsed);
      EXPECT_TRUE(parsed);
      EXPECT_LE(displayed, maximum + 0.00005);
      EXPECT_NEAR(width->value(), owner_width(), 1.0e-7);
    }

    // Rounded display text must not quantize the canonical cap on a plain
    // focus/Return cycle or when changing the selected language.
    width->setValue(maximum);
    finish_spin_edit(*width);
    const double precise_owner = owner_width();
    const QPoint center = line->rect().center();
    send_mouse_event(*line, QEvent::MouseButtonPress, center, Qt::LeftButton,
                     Qt::LeftButton);
    send_mouse_event(*line, QEvent::MouseButtonRelease, center, Qt::LeftButton,
                     Qt::NoButton);
    send_key_event(*line, Qt::Key_Return);
    EXPECT_DOUBLE_EQ(owner_width(), precise_owner);
    quader::LocalizationService italian;
    ASSERT_TRUE(italian.load(
        std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
        "it-IT"));
    panel.set_localization(italian);
    // Localization rebuilds descriptor editors. The retained pointer contract
    // above applies to value refreshes, not this explicit presentation rebuild.
    EXPECT_TRUE(retained_width.isNull());
    width = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
        "tool_settings.control.operation.edge_bevel.width"));
    ASSERT_NE(width, nullptr);
    line = width->findChild<QLineEdit *>();
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(maximum_hint(), nullptr);
    EXPECT_TRUE(width->toolTip().isEmpty());
    EXPECT_NEAR(owner_width(), precise_owner, 1.0e-7);

    ASSERT_TRUE(editor.tools().set_setting(
        "edge_bevel", "operation.edge_bevel.segments", 1.0).ok());
    panel.refresh();
    profile = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
        "tool_settings.control.operation.edge_bevel.profile"));
    ASSERT_NE(profile, nullptr);
    ASSERT_TRUE(profile->isEnabled());
    EXPECT_TRUE(profile->toolTip().isEmpty());
    EXPECT_TRUE(profile->parentWidget()->toolTip().isEmpty());
    auto *profile_line = profile->findChild<QLineEdit *>();
    ASSERT_NE(profile_line, nullptr);
    type_spin_text(*profile, profile->locale().toString(0.6, 'f', 1));
    send_key_event(*profile_line, Qt::Key_Return);
    const auto edited = editor.tools().active_settings();
    const auto *edited_profile = find_setting(edited, "operation.edge_bevel.profile");
    ASSERT_NE(edited_profile, nullptr);
    EXPECT_TRUE(edited_profile->enabled);
    EXPECT_DOUBLE_EQ(edited_profile->number_value, 0.6);
    EXPECT_DOUBLE_EQ(profile->value(), 0.6);
    EXPECT_EQ(maximum_hint(), nullptr);
    panel.unbind();
  }
}

TEST(QtToolSettingsPanelTests,
     BevelQueuedOvershootReconcilesWhenTheAcceptedLimitDoesNotChange) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-bevel-queued-limit-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  const auto source = editor.modeling().create_box(
      {.min = {-0.25F, -0.125F, -0.25F}, .max = {0.25F, 2.125F, 0.25F}},
      "Queued Bevel Limit Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 2, 3).ok());
  ASSERT_TRUE(editor.commands().execute(
      editor.commands().command_id_for_modeling_action("bevel_edges")).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 0.1).ok());
  ASSERT_TRUE(editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.segments", 2.0).ok());

  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  panel.activateWindow();
  QCoreApplication::processEvents();
  auto *width = panel.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width"));
  ASSERT_NE(width, nullptr);
  auto *line = width->findChild<QLineEdit *>();
  ASSERT_NE(line, nullptr);
  auto *maximum = panel.findChild<QLabel *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width.maximum"));
  EXPECT_EQ(maximum, nullptr);
  ASSERT_DOUBLE_EQ(width->value(), 0.1);
  ASSERT_GT(width->maximum(), 1.0);
  const QPointer<QDoubleSpinBox> retained_width(width);
  const QPoint crossed = begin_numeric_scrub(*line);
  const QPoint overshoot = crossed + QPoint(100, 0);

  // Another public API consumer can discover the source limit while this
  // gesture retains its draft and original bound. The next pointer sample
  // captures the updated owner revision, then normalizes to an unchanged
  // accepted value; it still needs immediate numeric feedback.
  const auto external_update = editor.tools().set_setting(
      "edge_bevel", "operation.edge_bevel.width", 1.0);
  ASSERT_TRUE(external_update.ok()) << external_update.error_message();
  ASSERT_TRUE(external_update.changed());
  const auto externally_accepted = editor.tools().active_settings();
  const auto *external_width =
      find_setting(externally_accepted, "operation.edge_bevel.width");
  ASSERT_NE(external_width, nullptr);
  ASSERT_LT(external_width->max_value, 1.0);
  ASSERT_DOUBLE_EQ(external_width->number_value, external_width->max_value);
  const auto accepted_settings_revision = editor.tools().active_settings_revision();
  const auto accepted_asset_revision = editor.revisions().asset_revision;
  ASSERT_DOUBLE_EQ(width->value(), 0.1);
  ASSERT_GT(width->maximum(), 1.0);
  send_mouse_event(*line, QEvent::MouseMove, overshoot, Qt::NoButton,
                   Qt::LeftButton);
  ASSERT_GT(width->value(), 1.0);
  ASSERT_TRUE(process_events_until([&] {
    return width->maximum() < 1.0 && width->value() < 1.0;
  }, 1000));
  EXPECT_EQ(editor.tools().active_settings_revision(), accepted_settings_revision);
  EXPECT_EQ(editor.revisions().asset_revision, accepted_asset_revision);
  EXPECT_EQ(panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.width")),
            retained_width.data());
  EXPECT_TRUE(quader::qt_install_numeric_spin_box(*width).is_dragging());
  const auto accepted = editor.tools().active_settings();
  const auto *accepted_width = find_setting(accepted, "operation.edge_bevel.width");
  ASSERT_NE(accepted_width, nullptr);
  EXPECT_DOUBLE_EQ(width->value(), accepted_width->number_value);
  EXPECT_DOUBLE_EQ(width->maximum(), accepted_width->max_value);

  send_key_event(*line, Qt::Key_Escape);
  EXPECT_FALSE(quader::qt_install_numeric_spin_box(*width).is_dragging());
  EXPECT_DOUBLE_EQ(width->value(), 0.1);
  const auto canceled = editor.tools().active_settings();
  const auto *canceled_width = find_setting(canceled, "operation.edge_bevel.width");
  ASSERT_NE(canceled_width, nullptr);
  EXPECT_DOUBLE_EQ(canceled_width->number_value, 0.1);
  panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     BevelWidthCustomSpinboxUsesFineAndLiveGridSteps) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-bevel-numeric-step-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));

  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto source = editor.modeling().create_box(
      {.min = {-10.0F, -10.0F, -10.0F}, .max = {10.0F, 10.0F, 10.0F}},
      "Bevel Numeric Step Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(source.value(), 1, 2).ok());
  ASSERT_TRUE(editor.commands()
                  .execute(editor.commands().command_id_for_modeling_action(
                      "bevel_edges"))
                  .ok());
  ASSERT_TRUE(editor.tools()
                  .set_setting("edge_bevel", "operation.edge_bevel.width", 0.1)
                  .ok());

  quader::QtToolSettingsPanel panel;
  panel.set_expanded(true);
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.bind(editor);
  panel.show();
  panel.activateWindow();
  QCoreApplication::processEvents();
  auto *width = panel.findChild<QDoubleSpinBox *>(
      QStringLiteral("tool_settings.control.operation.edge_bevel.width"));
  ASSERT_NE(width, nullptr);
  ASSERT_NE(dynamic_cast<quader::QtDoubleSpinBox *>(width), nullptr);
  auto *line_edit = width->findChild<QLineEdit *>();
  auto *increase = width->findChild<QToolButton *>(
      QStringLiteral("numeric_spin_box.increase"));
  ASSERT_NE(line_edit, nullptr);
  ASSERT_NE(increase, nullptr);
  EXPECT_DOUBLE_EQ(width->singleStep(), 0.01);
  const QPointer<QDoubleSpinBox> retained_width(width);
  double current_grid = 1.0;
  quader::qt_set_numeric_snap_increment_provider(
      application, panel, [&current_grid]() { return current_grid; });
  const auto owner_width = [&editor]() {
    const auto target = editor.tools().active_settings();
    const auto *setting = find_setting(target, "operation.edge_bevel.width");
    EXPECT_NE(setting, nullptr);
    return setting != nullptr ? setting->number_value : 0.0;
  };

  increase->click();
  EXPECT_DOUBLE_EQ(width->value(), 0.11);
  ASSERT_TRUE(process_events_until([&] {
    return std::abs(owner_width() - 0.11) <= 1.0e-12;
  }, 1000));

  struct ScrubCase {
    Qt::KeyboardModifiers modifiers;
    int pixels;
    double grid;
    double expected;
  };
  constexpr std::array<ScrubCase, 4> kCases{{
      {Qt::NoModifier, 1, 1.0, 0.11},
      {Qt::ShiftModifier, 1, 1.0, 0.101},
      {Qt::ControlModifier, 60, 1.0, 1.0},
      {Qt::ControlModifier, 60, 0.5, 0.5},
  }};
  for (const auto &test_case : kCases) {
    SCOPED_TRACE(test_case.expected);
    ASSERT_TRUE(editor.tools()
                    .set_setting("edge_bevel", "operation.edge_bevel.width", 0.1)
                    .ok());
    width->clearFocus();
    panel.refresh();
    ASSERT_EQ(panel.findChild<QDoubleSpinBox *>(QStringLiteral(
                  "tool_settings.control.operation.edge_bevel.width")),
              retained_width.data());
    ASSERT_DOUBLE_EQ(width->value(), 0.1);
    // Ctrl reads the current grid from the same retained field on each gesture.
    current_grid = test_case.grid;
    const QPoint crossed = begin_numeric_scrub(*line_edit, test_case.modifiers);
    ASSERT_TRUE(quader::qt_install_numeric_spin_box(*width).is_dragging());
    const QPoint moved = crossed + QPoint(test_case.pixels, 0);
    send_mouse_event(*line_edit, QEvent::MouseMove, moved, Qt::NoButton,
                     Qt::LeftButton, test_case.modifiers);
    EXPECT_DOUBLE_EQ(width->value(), test_case.expected);
    send_mouse_event(*line_edit, QEvent::MouseButtonRelease, moved,
                     Qt::LeftButton, Qt::NoButton, test_case.modifiers);
    EXPECT_FALSE(panel.numeric_interaction_active());
    EXPECT_DOUBLE_EQ(owner_width(), test_case.expected);
  }
  panel.unbind();
}

TEST(QtToolSettingsNumberSliderTests,
     BevelPointerMovesCoalesceAndFrameSyncRetainsEditors) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-bevel-numeric-tests";
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
          "Bevel Numeric Dispatch Source");
  ASSERT_TRUE(source.ok()) << source.error_message();
  ASSERT_TRUE(editor.selection().select_object(source.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());

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

  // Keep the application window alive when an empty overlay hides itself.
  // A hidden offscreen top-level widget loses activation on Windows; the real
  // overlay is a child of a viewport whose application window stays active.
  QWidget host;
  host.setAttribute(Qt::WA_DontShowOnScreen, true);
  QHBoxLayout host_layout(&host);
  quader::QtToolSettingsPanel window(&host);
  host_layout.addWidget(&window);
  window.set_expanded(true);
  window.bind(editor);
  window.show();
  host.show();
  host.activateWindow();
  QCoreApplication::processEvents();

  auto *width_spin = window.findChild<QDoubleSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.width"));
  ASSERT_NE(width_spin, nullptr);
  ASSERT_NE(dynamic_cast<quader::QtDoubleSpinBox *>(width_spin), nullptr);
  ASSERT_TRUE(window.findChildren<QSlider *>().isEmpty());
  auto *width_editor = width_spin->findChild<QLineEdit *>();
  ASSERT_NE(width_editor, nullptr);
  auto &width_controller = quader::qt_install_numeric_spin_box(*width_spin);
  QPointer<QDoubleSpinBox> retained_width_spin(width_spin);
  int width_spin_changes = 0;
  int width_editor_destroys = 0;
  QObject::connect(width_spin, &QDoubleSpinBox::valueChanged,
                   [&width_spin_changes](double) { ++width_spin_changes; });
  QObject::connect(width_spin, &QObject::destroyed,
                   [&width_editor_destroys]() { ++width_editor_destroys; });

  const QPoint width_crossed = begin_numeric_scrub(*width_editor);
  const QPoint width_move = width_crossed + QPoint(1, 0);
  ASSERT_TRUE(width_controller.is_dragging());
  EXPECT_TRUE(window.numeric_interaction_active());
  const quader::QuaderRevisionStamp width_revisions_before =
      editor.revisions();
  const auto width_start = std::chrono::steady_clock::now();
  send_mouse_event(*width_editor, QEvent::MouseMove, width_move,
                   Qt::NoButton, Qt::LeftButton);
  const auto width_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                width_start);

  EXPECT_EQ(width_spin_changes, 1);
  EXPECT_EQ(width_editor_destroys, 0);
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
  EXPECT_EQ(width_spin_changes, 1);
  EXPECT_EQ(width_editor_destroys, 0);
  EXPECT_EQ(window.findChild<QDoubleSpinBox *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.width")),
            retained_width_spin.data());
  EXPECT_TRUE(width_controller.is_dragging());
  EXPECT_TRUE(window.numeric_interaction_active());
  EXPECT_DOUBLE_EQ(width_spin->value(), pending_width);
  const auto width_release_start = std::chrono::steady_clock::now();
  send_mouse_event(*width_editor, QEvent::MouseButtonRelease, width_move,
                   Qt::LeftButton, Qt::NoButton);
  const auto width_release_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                width_release_start);
  EXPECT_FALSE(width_controller.is_dragging());
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_EQ(width_spin_changes, 1);
  EXPECT_GT(editor.revisions().asset_revision,
            width_revisions_before.asset_revision);
  const quader::QuaderToolSettingsTarget applied_width_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *applied_width_setting =
      find_setting(applied_width_settings, "operation.edge_bevel.width");
  ASSERT_NE(applied_width_setting, nullptr);
  EXPECT_DOUBLE_EQ(applied_width_setting->number_value, pending_width);

  auto *segments_spin = window.findChild<QSpinBox *>(QStringLiteral(
      "tool_settings.control.operation.edge_bevel.segments"));
  ASSERT_NE(segments_spin, nullptr);
  ASSERT_NE(dynamic_cast<quader::QtSpinBox *>(segments_spin), nullptr);
  auto *segments_editor = segments_spin->findChild<QLineEdit *>();
  ASSERT_NE(segments_editor, nullptr);
  auto &segments_controller =
      quader::qt_install_numeric_spin_box(*segments_spin);
  QPointer<QSpinBox> retained_segments_spin(segments_spin);
  int segments_spin_changes = 0;
  int segments_editor_destroys = 0;
  QObject::connect(segments_spin, &QSpinBox::valueChanged,
                   [&segments_spin_changes](int) { ++segments_spin_changes; });
  QObject::connect(segments_spin, &QObject::destroyed,
                   [&segments_editor_destroys]() { ++segments_editor_destroys; });

  const QPoint segments_crossed = begin_numeric_scrub(*segments_editor);
  const std::array<QPoint, 5> segments_moves{
      segments_crossed + QPoint(16, 0), segments_crossed + QPoint(32, 0),
      segments_crossed + QPoint(48, 0), segments_crossed + QPoint(64, 0),
      segments_crossed + QPoint(80, 0),
  };
  ASSERT_TRUE(segments_controller.is_dragging());
  const quader::QuaderRevisionStamp segments_revisions_before =
      editor.revisions();
  const std::uint64_t segments_settings_revision_before =
      editor.tools().active_settings_revision();
  const quader::ModelingSdkProjectionStats segments_projection_before =
      session.modeling().modeling_sdk_projection_stats_for_tests();
  const auto segments_start = std::chrono::steady_clock::now();
  for (std::size_t index = 0; index < 3; ++index) {
    send_mouse_event(*segments_editor, QEvent::MouseMove,
                     segments_moves[index], Qt::NoButton, Qt::LeftButton);
  }
  const auto segments_elapsed = std::chrono::duration_cast<
      std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                segments_start);

  EXPECT_EQ(segments_spin_changes, 3);
  EXPECT_EQ(segments_editor_destroys, 0);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_before.asset_revision);
  const int pending_segments = segments_spin->value();
  const quader::QuaderToolSettingsTarget pending_segments_settings =
      editor.tools().active_settings();
  const quader::QuaderToolSettingControl *pending_segments_setting =
      find_setting(pending_segments_settings, "operation.edge_bevel.segments");
  ASSERT_NE(pending_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(pending_segments_setting->number_value, 1.0);
  EXPECT_NE(pending_segments_setting->number_value,
            static_cast<double>(pending_segments));
  EXPECT_TRUE(window.numeric_interaction_active());

  window.refresh();
  EXPECT_EQ(segments_spin_changes, 3);
  EXPECT_EQ(segments_editor_destroys, 0);
  EXPECT_EQ(window.findChild<QSpinBox *>(QStringLiteral(
                "tool_settings.control.operation.edge_bevel.segments")),
            retained_segments_spin.data());
  EXPECT_TRUE(segments_controller.is_dragging());
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
      find_setting(live_segments_settings, "operation.edge_bevel.segments");
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
    send_mouse_event(*segments_editor, QEvent::MouseMove,
                     segments_moves[index], Qt::NoButton, Qt::LeftButton);
  }
  EXPECT_EQ(segments_spin_changes, 5);
  EXPECT_EQ(editor.revisions().asset_revision,
            segments_revisions_after_live_update.asset_revision);
  EXPECT_TRUE(window.numeric_interaction_active());
  const int final_pending_segments = segments_spin->value();
  EXPECT_NE(final_pending_segments, pending_segments);
  window.refresh();
  EXPECT_EQ(segments_editor_destroys, 0);
  EXPECT_EQ(segments_spin->value(), final_pending_segments);
  send_mouse_event(*segments_editor, QEvent::MouseButtonRelease,
                   segments_moves.back(), Qt::LeftButton, Qt::NoButton);
  EXPECT_FALSE(segments_controller.is_dragging());
  EXPECT_FALSE(window.numeric_interaction_active());
  EXPECT_EQ(segments_spin_changes, 5);
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
      find_setting(applied_segments_settings, "operation.edge_bevel.segments");
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
  EXPECT_EQ(segments_editor_destroys, 0);
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

  // Establish default owner settings before checking queued Undo/Cancel edits.
  ASSERT_TRUE(editor.tools().reset_active_settings().ok());
  window.refresh();
  EXPECT_FALSE(window.numeric_interaction_active());
  const auto reset_segments_target = editor.tools().active_settings();
  const quader::QuaderToolSettingControl *reset_segments_setting =
      find_setting(reset_segments_target,
                   "operation.edge_bevel.segments");
  ASSERT_NE(reset_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(reset_segments_setting->number_value, 1.0);

  // Ctrl+Z semantics cancel a live preview before touching document history.
  // A value queued against that preview epoch must disappear rather than
  // mutating the persistent interaction setting after the timer wakes.
  segments_spin->setFocus(Qt::OtherFocusReason);
  QCoreApplication::processEvents();
  const int stale_undo_value = 2;
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
  // Undo hid the empty overlay. Its standalone host restores visibility when
  // settings return, as QtViewportPanel does in the application.
  window.show();
  QCoreApplication::processEvents();
  ASSERT_TRUE(host.isActiveWindow());
  ASSERT_TRUE(window.isVisible());
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

  RecordProperty("width_numeric_callbacks", width_spin_changes);
  RecordProperty("width_editor_rebuilds", width_editor_destroys);
  RecordProperty("width_pointer_move_us",
                 static_cast<int>(width_elapsed.count()));
  RecordProperty("width_final_flush_us",
                 static_cast<int>(width_release_elapsed.count()));
  RecordProperty("segments_numeric_callbacks", segments_spin_changes);
  RecordProperty("segments_editor_rebuilds", segments_editor_destroys);
  RecordProperty("segments_owner_dispatch_batches", 2);
  RecordProperty("segments_spin_coalesced_callbacks",
                 segments_spin_changes - spin_changes_before);
  RecordProperty("segments_three_pointer_moves_us",
                 static_cast<int>(segments_elapsed.count()));

  window.unbind();
  window.hide();
}

TEST(QtFaceTextureControlsTests,
     UvSemanticHelpIsLocalizedRetainedAndSkippedOnUnchangedFrames) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-uv-semantic-help-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  auto standalone = quader::create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = create_texture_action_surface(editor);
  ASSERT_TRUE(created.ok());
  ASSERT_TRUE(editor.selection().select_object(created.value()).ok());
  ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  quader::QtFaceTextureControls panel;
  panel.bind(editor);

  const std::array<std::pair<const char *, const char *>, 12> help_bindings{{
      {"face_texture.edit_texture.align_to_grid",
       "tool_settings.edit_texture.align_to_grid.tooltip"},
      {"face_texture.edit_texture.align_to_face",
       "tool_settings.edit_texture.align_to_face.tooltip"},
      {"face_texture.edit_texture.align_to_view",
       "tool_settings.edit_texture.align_to_view.tooltip"},
      {"face_texture.edit_texture.flip_face_horizontal",
       "tool_settings.edit_texture.flip_face_horizontal.tooltip"},
      {"face_texture.edit_texture.justify_left",
       "tool_settings.edit_texture.justify_left.tooltip"},
      {"face_texture.edit_texture.rotate_clockwise",
       "tool_settings.edit_texture.rotate_clockwise.tooltip"},
      {"face_texture.edit_texture.treat_as_one",
       "tool_settings.edit_texture.treat_as_one_tooltip"},
      {"face_texture.texture.scale_x", "tool_settings.texture_scale_x.tooltip"},
      {"face_texture.texture.scale_y", "tool_settings.texture_scale_y.tooltip"},
      {"face_texture.texture.shift_x", "tool_settings.texture_shift_x.tooltip"},
      {"face_texture.texture.shift_y", "tool_settings.texture_shift_y.tooltip"},
      {"face_texture.texture.rotate", "tool_settings.texture_rotate.tooltip"},
  }};
  std::array<QPointer<QWidget>, help_bindings.size()> retained;
  for (std::size_t index = 0; index < help_bindings.size(); ++index) {
    retained[index] = panel.findChild<QWidget *>(
        QString::fromUtf8(help_bindings[index].first));
    ASSERT_NE(retained[index], nullptr);
  }
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  const auto content_revision = editor.revisions().modeling_content_revision;
  for (const char *locale : {"en-US", "it-IT"}) {
    SCOPED_TRACE(locale);
    ASSERT_TRUE(localization.select_locale(locale));
    panel.set_localization(localization);
    QCoreApplication::processEvents();
    for (std::size_t index = 0; index < help_bindings.size(); ++index) {
      auto *widget = panel.findChild<QWidget *>(
          QString::fromUtf8(help_bindings[index].first));
      ASSERT_EQ(widget, retained[index].data());
      const auto help = localization.text(help_bindings[index].second, "");
      ASSERT_FALSE(help.empty());
      EXPECT_TRUE(widget->toolTip().contains(QString::fromStdString(help)));
    }
    EXPECT_EQ(retained[9]->accessibleName(), QString::fromStdString(
        localization.text("tool_settings.texture_shift_x", "")));
    EXPECT_EQ(retained[11]->accessibleName(), QString::fromStdString(
        localization.text("tool_settings.texture_rotate", "")));
    EXPECT_TRUE(retained[0]->toolTip().contains(QStringLiteral("0.125")) ||
                retained[0]->toolTip().contains(QStringLiteral("0,125")));
    TexturePresentationEvents events;
    for (const auto &widget : retained) {
      widget->installEventFilter(&events);
    }
    for (int frame = 0; frame < 20; ++frame) {
      panel.refresh();
    }
    EXPECT_EQ(events.changes, 0);
    EXPECT_EQ(editor.revisions().modeling_content_revision, content_revision);
  }

  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::UvPeel).ok());
  quader::QtToolSettingsPanel tool_panel;
  tool_panel.set_expanded(true);
  tool_panel.bind(editor);
  const std::array<std::pair<const char *, const char *>, 5> peel_bindings{{
      {"tool_settings.control.uv_peel.world_space",
       "tool_settings.uv_peel.world_space_tooltip"},
      {"tool_settings.control.uv_peel.u_repeat", "tool_settings.uv_peel.u_repeat.tooltip"},
      {"tool_settings.control.uv_peel.v_repeat", "tool_settings.uv_peel.v_repeat.tooltip"},
      {"tool_settings.control.uv_peel.u_offset", "tool_settings.uv_peel.u_offset.tooltip"},
      {"tool_settings.control.uv_peel.v_offset", "tool_settings.uv_peel.v_offset.tooltip"},
  }};
  for (const char *locale : {"en-US", "it-IT"}) {
    SCOPED_TRACE(locale);
    ASSERT_TRUE(localization.select_locale(locale));
    tool_panel.set_localization(localization);
    QCoreApplication::processEvents();
    std::array<QPointer<QWidget>, peel_bindings.size()> peel_retained;
    TexturePresentationEvents events;
    for (std::size_t index = 0; index < peel_bindings.size(); ++index) {
      auto *widget = tool_panel.findChild<QWidget *>(
          QString::fromUtf8(peel_bindings[index].first));
      ASSERT_NE(widget, nullptr);
      peel_retained[index] = widget;
      const auto help = localization.text(peel_bindings[index].second, "");
      ASSERT_FALSE(help.empty());
      EXPECT_TRUE(widget->toolTip().contains(QString::fromStdString(help)));
      widget->installEventFilter(&events);
    }
    EXPECT_EQ(peel_retained[0]->accessibleName(), QString::fromStdString(
        localization.text("tool_settings.uv_peel.world_space", "")));
    EXPECT_TRUE(peel_retained[0]->toolTip().contains(QStringLiteral("64")));
    const auto settings_revision = editor.tools().active_settings_revision();
    for (int frame = 0; frame < 20; ++frame) {
      tool_panel.refresh();
    }
    EXPECT_EQ(events.changes, 0);
    EXPECT_EQ(editor.tools().active_settings_revision(), settings_revision);
    EXPECT_EQ(editor.revisions().modeling_content_revision, content_revision);
    for (std::size_t index = 0; index < peel_bindings.size(); ++index) {
      EXPECT_EQ(tool_panel.findChild<QWidget *>(
                    QString::fromUtf8(peel_bindings[index].first)),
                peel_retained[index].data());
    }
  }
  tool_panel.unbind();
}

TEST(QtToolSettingsPanelTests,
     EveryControlUsesAlignedLabelAndFieldRowsAcrossLocalesAndScales) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char executable[] = "quader-tool-settings-row-layout-tests";
  char *argv[] = {executable, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  quader::apply_qt_obsidian_application_theme(
      QApplication::palette());
  quader::QtUiScaleApplicationController scale_controller(application);
  quader::LocalizationService localization;
  ASSERT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales", "en-US"));
  QString screenshot_directory =
      qEnvironmentVariable("QUADER_TOOL_SETTINGS_ROWS_SCREENSHOT_DIR");
  if (screenshot_directory.isEmpty()) {
    screenshot_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  }
  if (!screenshot_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(screenshot_directory));
  }
  std::array<bool, 5> kinds_seen{};
  std::array<bool, 3> choices_seen{};
  for (const auto tool : {
           quader::ViewportToolId::Move, quader::ViewportToolId::Cut,
           quader::ViewportToolId::CreatePrimitive,
           quader::ViewportToolId::RadialArray,
           quader::ViewportToolId::PathArray, quader::ViewportToolId::Curve}) {
    SCOPED_TRACE(static_cast<int>(tool));
    auto standalone = quader::create_standalone_editor();
    auto &editor = standalone.editor();
    const bool uses_curve = tool == quader::ViewportToolId::PathArray ||
                            tool == quader::ViewportToolId::Curve;
    const auto object = uses_curve
        ? editor.modeling().create_curve({
              .points = {{.position = {0.0F, 0.0F, 0.0F}},
                         {.position = {32.0F, 0.0F, 0.0F}}},
              .name = "Settings Row Curve",
          })
        : editor.modeling().create_box(
              {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
              "Settings Row Box");
    ASSERT_TRUE(object.ok()) << object.error_message();
    ASSERT_TRUE(editor.selection().select_object(object.value()).ok());
    if (tool == quader::ViewportToolId::Move) {
      ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Vertex).ok());
    }
    ASSERT_TRUE(editor.tools().activate(tool).ok());
    quader::QtToolSettingsPanel panel;
    panel.setAttribute(Qt::WA_DontShowOnScreen, true);
    panel.bind(editor);
    panel.show();
    for (const char *locale : {"en-US", "it-IT"}) {
      SCOPED_TRACE(locale);
      ASSERT_TRUE(localization.select_locale(locale));
      panel.set_localization(localization);
      for (auto *header : panel.findChildren<QToolButton *>()) {
        if (header->objectName().startsWith(QStringLiteral("tool_settings.group."))) {
          header->setChecked(true);
        }
      }
      for (float scale : {1.0F, 1.5F, 2.0F}) {
        SCOPED_TRACE(scale);
        scale_controller.apply_factor(scale);
        panel.apply_ui_scale(scale_controller.metrics());
        process_events_for(20);
        panel.refresh();
        panel.resize(panel.sizeHint());
        process_events_for(20);
        auto *operation_header = panel.findChild<QToolButton *>(
            QStringLiteral("tool_settings.target_title"));
        ASSERT_NE(operation_header, nullptr);
        EXPECT_EQ(operation_header->geometry().left(), 0);
        EXPECT_EQ(operation_header->width(), panel.width());
        EXPECT_GE(operation_header->height(), scale_controller.metrics().px(26));
        EXPECT_EQ(panel.findChild<QWidget *>(
                      QStringLiteral("tool_settings.reset_defaults")), nullptr);
        EXPECT_TRUE(panel.findChildren<QSlider *>().isEmpty());
        const auto target = editor.tools().active_settings();
        ASSERT_FALSE(target.controls.empty());
        std::optional<int> field_left;
        std::optional<int> label_width;
        for (const auto &control : target.controls) {
          SCOPED_TRACE(control.id);
          kinds_seen[static_cast<std::size_t>(control.kind)] = true;
          if (control.kind == quader::QuaderToolSettingControlKind::Choice) {
            choices_seen[static_cast<std::size_t>(control.choice_presentation)] = true;
          }
          const QString name = QStringLiteral("tool_settings.control.") +
                               QString::fromStdString(control.id);
          auto *row = panel.findChild<QWidget *>(name + QStringLiteral(".row"));
          ASSERT_NE(row, nullptr);
          auto *layout = dynamic_cast<QGridLayout *>(row->layout());
          ASSERT_NE(layout, nullptr);
          ASSERT_NE(layout->itemAtPosition(0, 1), nullptr);
          auto *field = layout->itemAtPosition(0, 1)->widget();
          ASSERT_NE(field, nullptr);
          EXPECT_TRUE(row->rect().contains(field->geometry()));
          EXPECT_GE(field->height(), field->minimumSizeHint().height());
          if (field_left.has_value()) {
            EXPECT_EQ(field->geometry().left(), *field_left);
          } else {
            field_left = field->geometry().left();
          }
          if (control.kind != quader::QuaderToolSettingControlKind::Action) {
            auto *label = panel.findChild<QLabel *>(name + QStringLiteral(".label"));
            ASSERT_NE(label, nullptr);
            EXPECT_TRUE(label->isVisible());
            EXPECT_EQ(label->geometry().left(), row->contentsRect().left());
            EXPECT_GT(field->geometry().left(), label->geometry().right());
            if (control.kind !=
                quader::QuaderToolSettingControlKind::Vector3Number) {
              EXPECT_LE(std::abs(field->geometry().center().y() -
                                 label->geometry().center().y()), 1);
            }
            if (label_width.has_value()) {
              EXPECT_EQ(label->width(), *label_width);
            } else {
              label_width = label->width();
            }
            EXPECT_GE(label->height(), label->heightForWidth(label->width()));
            if (control.id == "auto_merge_vertices") {
              // This sole Move setting used to wrap within 88px while most of
              // the row stayed empty. Its actual label fits in either locale.
              EXPECT_GE(label->width(),
                        label->fontMetrics().horizontalAdvance(label->text()));
              EXPECT_LE(label->heightForWidth(label->width()),
                        label->fontMetrics().lineSpacing());
            }
          }
          if (control.kind == quader::QuaderToolSettingControlKind::Toggle) {
            auto *toggle = qobject_cast<QCheckBox *>(field);
            ASSERT_NE(toggle, nullptr);
            EXPECT_EQ(toggle->width(), toggle->sizeHint().width());
          }
          if (control.kind == quader::QuaderToolSettingControlKind::Number) {
            if (auto *spin = field->findChild<QSpinBox *>()) {
              EXPECT_EQ(spin->mapTo(row, QPoint()).x(), *field_left);
              EXPECT_EQ(spin->width(), field->width());
            }
          }
          if (control.kind == quader::QuaderToolSettingControlKind::Vector3Number) {
            std::optional<QRect> previous_axis;
            for (const auto &component : control.vector3_components) {
              const QString component_name =
                  QStringLiteral("tool_settings.control.") +
                  QString::fromStdString(component.id);
              auto *spin = panel.findChild<QDoubleSpinBox *>(component_name);
              auto *axis_label = panel.findChild<QLabel *>(
                  component_name + QStringLiteral(".axis_label"));
              ASSERT_NE(spin, nullptr);
              ASSERT_NE(axis_label, nullptr);
              EXPECT_TRUE(field->rect().contains(spin->geometry()));
              EXPECT_GE(spin->width(), scale_controller.metrics().px(88));
              EXPECT_LT(axis_label->geometry().right(), spin->geometry().left());
              EXPECT_LE(std::abs(axis_label->geometry().center().y() -
                                 spin->geometry().center().y()), 1);
              if (previous_axis.has_value()) {
                EXPECT_EQ(spin->geometry().left(), previous_axis->left());
                EXPECT_EQ(spin->geometry().width(), previous_axis->width());
                EXPECT_GT(spin->geometry().top(), previous_axis->bottom());
              } else {
                auto *label = panel.findChild<QLabel *>(
                    name + QStringLiteral(".label"));
                ASSERT_NE(label, nullptr);
                const int first_line_center = label->geometry().top() +
                    label->contentsRect().top() + label->fontMetrics().height() / 2;
                const int field_center = field->geometry().top() +
                    spin->geometry().center().y();
                EXPECT_LE(std::abs(first_line_center - field_center), 1);
              }
              previous_axis = spin->geometry();
              auto *editor = spin->findChild<QLineEdit *>();
              ASSERT_NE(editor, nullptr);
              const QMargins margins = editor->textMargins();
              EXPECT_GE(editor->contentsRect().width() - margins.left() -
                            margins.right() - 4,
                        editor->fontMetrics().horizontalAdvance(editor->text()))
                  << editor->text().toStdString();
            }
          }
        }
        if (!screenshot_directory.isEmpty()) {
          QImage screenshot(panel.size(), QImage::Format_ARGB32_Premultiplied);
          screenshot.fill(Qt::transparent);
          panel.render(&screenshot);
          EXPECT_TRUE(screenshot.save(QDir(screenshot_directory).filePath(
              QStringLiteral("tool-options-%1-%2-%3.png")
                  .arg(QString::fromStdString(target.tool_id),
                       QString::fromLatin1(locale))
                  .arg(static_cast<int>(std::lround(scale * 100.0F))))));
        }
      }
      // Finish the scale/font layout transition before measuring idle refresh.
      panel.refresh();
      process_events_for(20);
      TexturePresentationEvents events;
      for (auto *widget : panel.findChildren<QWidget *>()) {
        widget->installEventFilter(&events);
      }
      int host_notifications = 0;
      panel.presentation_changed = [&] { ++host_notifications; };
      for (int frame = 0; frame < 20; ++frame) {
        panel.refresh();
      }
      EXPECT_EQ(host_notifications, 0);
      EXPECT_EQ(events.changes, 0);
      panel.presentation_changed = {};
    }
    panel.unbind();
  }
  EXPECT_TRUE(std::all_of(kinds_seen.begin(), kinds_seen.end(),
                          [](bool seen) { return seen; }));
  EXPECT_TRUE(std::all_of(choices_seen.begin(), choices_seen.end(),
                          [](bool seen) { return seen; }));
}
