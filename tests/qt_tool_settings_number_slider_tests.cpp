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
#include "ui/qt_tool_settings_number_slider.hpp"
#include "ui/qt_tool_settings_panel.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/ui_panel_manager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDockWidget>
#include <QEventLoop>
#include <QKeyEvent>
#include <QLineEdit>
#include <QLabel>
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
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>
#include <QTimer>
#include <QToolButton>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <utility>

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

const quader::QuaderToolSettingControl *find_setting(
    const quader::QuaderToolSettingsTarget &target, std::string_view id) {
  const auto found = std::find_if(
      target.controls.begin(), target.controls.end(),
      [id](const quader::QuaderToolSettingControl &control) {
        return control.id == id;
      });
  return found == target.controls.end() ? nullptr : &*found;
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
  auto *description_content = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.description.content"));
  auto *settings_header = panel.findChild<QToolButton *>(
      QStringLiteral("tool_settings.section.settings.header"));
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
  ASSERT_NE(description_content, nullptr);
  ASSERT_NE(settings_header, nullptr);
  ASSERT_NE(settings_content, nullptr);
  ASSERT_NE(description, nullptr);
  ASSERT_NE(reset, nullptr);
  ASSERT_NE(copies_label, nullptr);
  ASSERT_NE(copies_slider, nullptr);
  EXPECT_TRUE(panel.has_active_target());
  EXPECT_FALSE(description->text().isEmpty());
  EXPECT_TRUE(description_header->isChecked());
  EXPECT_TRUE(settings_header->isChecked());
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
  const quader::QuaderToolSettingControl *reset_copies =
      find_setting(editor.tools().active_settings(), "radial_array.copies");
  ASSERT_NE(reset_copies, nullptr);
  EXPECT_DOUBLE_EQ(reset_copies->number_value, default_copies->number_value);

  ASSERT_TRUE(editor.tools().cancel_active_tool_preview().ok());
  panel.refresh();
  EXPECT_FALSE(panel.has_active_target());
  EXPECT_FALSE(panel.isHidden());
  EXPECT_FALSE(reset->isEnabled());
  auto *target_title = panel.findChild<QLabel *>(
      QStringLiteral("tool_settings.target_title"));
  ASSERT_NE(target_title, nullptr);
  EXPECT_EQ(target_title->text(), QStringLiteral("No Active Tool"));

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
  EXPECT_EQ(discard_left->text(), QStringLiteral("Scarta sinistra"));
  EXPECT_EQ(target_title->text(), QStringLiteral("Taglia"));
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
  panel.bind(editor);
  panel.show();
  QCoreApplication::processEvents();
  auto *section = panel.findChild<QWidget *>(
      QStringLiteral("tool_settings.section.texture"));
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
  ASSERT_NE(header, nullptr);
  ASSERT_NE(content, nullptr);
  ASSERT_NE(scale_x, nullptr);
  ASSERT_NE(scale_y, nullptr);
  ASSERT_NE(shift_x, nullptr);
  ASSERT_NE(shift_y, nullptr);
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
  EXPECT_TRUE(edit_header->isChecked());
  EXPECT_FALSE(treat_as_one->isChecked());
  EXPECT_EQ(edit_section->findChildren<QPushButton *>().size(), 25);
  EXPECT_TRUE(header->isChecked());
  EXPECT_FALSE(panel.has_active_target());
  EXPECT_TRUE(section->findChildren<QSlider *>().isEmpty());
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
  EXPECT_FALSE(section->isHidden());
  EXPECT_FALSE(edit_section->isHidden());
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
  process_events_for(75);
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

  type_spin_text(*scale, QStringLiteral("2.0"));
  ASSERT_TRUE(panel.numeric_interaction_active());
  up->click();
  EXPECT_NEAR(*editor.modeling().face_texture_settings().scale_x, 2.1, 0.000001);
  EXPECT_FALSE(panel.numeric_interaction_active());
  process_events_for(75);
  EXPECT_NEAR(*editor.modeling().face_texture_settings().scale_x, 2.1, 0.000001);
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
  type_spin_text(*scale, QStringLiteral("1"));
  process_events_for(75);
  EXPECT_EQ(editor.revisions().modeling_content_revision, invalid_revision);
  EXPECT_TRUE(message->isHidden());
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
  EXPECT_NEAR(*editor.modeling().face_texture_settings().scale_x, 3.1, 0.000001);
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
  EXPECT_NEAR(*editor.modeling().face_texture_settings().scale_x, 1.1, 0.000001);

  ASSERT_TRUE(set_face_texture_field(
                  editor, quader::QuaderFaceTextureField::ScaleX, 0.05).ok());
  panel.refresh_texture_settings();
  const auto failure_revision = editor.revisions().modeling_content_revision;
  const auto failure_history = editor.history().snapshot();
  down->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, failure_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, failure_history.undo_label);
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(message->text().isEmpty());
  flip->click();
  EXPECT_EQ(editor.revisions().modeling_content_revision, failure_revision);
  EXPECT_FALSE(message->text().isEmpty());

  ASSERT_TRUE(editor.modeling().begin_transform_preview().ok());
  panel.refresh_texture_settings();
  EXPECT_FALSE(up->isEnabled());
  EXPECT_FALSE(flip->isEnabled());
  EXPECT_FALSE(message->isHidden());
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
  process_events_for(75);
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
  ASSERT_NE(axis, nullptr);
  ASSERT_NE(world_space, nullptr);
  ASSERT_NE(apply, nullptr);
  ASSERT_NE(help, nullptr);
  EXPECT_TRUE(section->isHidden());
  EXPECT_TRUE(header->isChecked());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Face).ok());
  ASSERT_TRUE(editor.selection().select_face(created.value(), 1).ok());
  panel->refresh();
  EXPECT_TRUE(section->isHidden());
  EXPECT_FALSE(face_section->isHidden());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 1, 2).ok());
  panel->refresh();
  QCoreApplication::processEvents();
  EXPECT_FALSE(section->isHidden());
  EXPECT_TRUE(face_section->isHidden());
  EXPECT_FALSE(panel->has_active_target());
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
  process_events_for(75);
  const quader::QuaderUvPeelOptions expected{
      .axis = quader::QuaderUvPeelAxis::V, .world_space = false,
      .u_repeat = 2.0, .v_repeat = 3.0, .u_offset = 0.25, .v_offset = -0.5};
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
  EXPECT_TRUE(section->isHidden());
  ASSERT_TRUE(editor.selection().select_edge(created.value(), 2, 3).ok());
  panel->refresh_uv_peel_settings();
  EXPECT_TRUE(dock->isHidden());
  EXPECT_FALSE(section->isHidden());
  EXPECT_TRUE(content->isHidden());
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, expected);
  panel->unbind();
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
  const auto before = editor.revisions().modeling_content_revision;
  const auto before_history = editor.history().snapshot();
  for (const QString &invalid : {QStringLiteral("0"), QStringLiteral("-")}) {
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
  }
  for (int toggle = 0; toggle < 2; ++toggle) {
    const QPoint indicator(7, world_space->height() / 2);
    send_mouse_event(*world_space, QEvent::MouseButtonPress, indicator,
                     Qt::LeftButton, Qt::LeftButton);
    send_mouse_event(*world_space, QEvent::MouseButtonRelease, indicator,
                     Qt::LeftButton, Qt::NoButton);
    EXPECT_EQ(world_space->isChecked(), toggle != 0);
  }
  process_events_for(75);
  auto *repeat_text = repeat->findChild<QLineEdit *>();
  ASSERT_NE(repeat_text, nullptr);
  EXPECT_EQ(repeat_text->text(), QStringLiteral("-"));
  EXPECT_EQ(editor.modeling().uv_peel_settings().options,
            quader::QuaderUvPeelOptions{});
  EXPECT_TRUE(panel.numeric_interaction_active());
  // Accepting a correction to the stored value clears the validation message
  // even when the owner need not advance its settings revision.
  type_spin_text(*repeat, QStringLiteral("1"));
  process_events_for(75);
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
  process_events_for(75);
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
  process_events_for(75);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, external_options);
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
  apply->click();
  quader::QuaderUvPeelOptions accepted_options = external_options;
  accepted_options.u_repeat = 7.0;
  EXPECT_EQ(editor.revisions().modeling_content_revision, branched_revision);
  EXPECT_EQ(editor.history().snapshot().undo_label, branched_history.undo_label);
  EXPECT_EQ(editor.modeling().uv_peel_settings().options, accepted_options);
  EXPECT_FALSE(message->isHidden());
  EXPECT_FALSE(message->text().isEmpty());
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
  process_events_for(75);
  EXPECT_DOUBLE_EQ(editor.modeling().face_texture_settings().rotation_degrees,
                   10.0);
  rotate->setValue(20.0);
  process_events_for(75);
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
  process_events_for(75);
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

  const quader::QuaderToolSettingControl *live_setting = find_setting(
      editor.tools().active_settings(), "primitive.box.subdivisions_x");
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
  process_events_for(100);
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
  const quader::QuaderToolSettingControl *spin_applied_setting = find_setting(
      editor.tools().active_settings(), "operation.edge_bevel.segments");
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
  const quader::QuaderToolSettingControl *reset_segments_setting =
      find_setting(editor.tools().active_settings(),
                   "operation.edge_bevel.segments");
  ASSERT_NE(reset_segments_setting, nullptr);
  EXPECT_DOUBLE_EQ(reset_segments_setting->number_value, 1.0);
  process_events_for(75);
  reset_segments_setting = find_setting(editor.tools().active_settings(),
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
