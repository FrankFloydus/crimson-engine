////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_editor_api.hpp"
#include "ui/qt_face_texture_canvas.hpp"
#include "ui/qt_face_texture_controls.hpp"
#include "ui/qt_face_texture_panel.hpp"
#include "ui/qt_material_browser_window.hpp"
#include "ui/qt_tool_settings_panel.hpp"

#include <QApplication>
#include <QBuffer>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QWheelEvent>

#include <array>
#include <cmath>
#include <memory>
#include <optional>

namespace quader {
namespace {

class QtFaceTextureCanvasTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_.data());
    QApplication::setStyle(QStringLiteral("Fusion"));
    standalone_.emplace(create_standalone_editor());
    const auto created = editor().modeling().create_box(
        {.min = {-64.0f, -32.0f, -16.0f},
         .max = {64.0f, 32.0f, 16.0f}},
        "Face Texture Canvas");
    ASSERT_TRUE(created.ok()) << created.error_message();
    object_ = created.value();
    ASSERT_TRUE(editor().selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor().selection().select_face(object_, 1).ok());
    canvas_ = std::make_unique<QtFaceTextureCanvas>();
    canvas_->setAttribute(Qt::WA_DontShowOnScreen);
    canvas_->resize(420, 320);
    canvas_->bind(editor());
    canvas_->show();
    QApplication::setActiveWindow(canvas_.get());
    canvas_->setFocus();
    QCoreApplication::processEvents();
  }

  void TearDown() override {
    if (canvas_ != nullptr) {
      canvas_->abandon();
    }
    canvas_.reset();
    standalone_.reset();
    application_.reset();
  }

  QuaderEditorApi &editor() { return standalone_->editor(); }

  void mouse(QEvent::Type type, QPointF location, Qt::MouseButton button,
              Qt::MouseButtons buttons,
              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    const QPointF global = canvas_->mapToGlobal(location.toPoint());
    QMouseEvent event(type, location, global, button, buttons, modifiers);
    QCoreApplication::sendEvent(canvas_.get(), &event);
  }

  QRectF plane_bounds() const {
    QPolygonF polygon;
    for (const auto point : canvas_->presentation().outline) {
      polygon.push_back(QPointF(point.x, point.y));
    }
    return polygon.boundingRect();
  }

  QPointF background_location() const {
    return canvas_->plane_to_view().map(
        plane_bounds().center() + QPointF(plane_bounds().width() * 0.2,
                                          plane_bounds().height() * 0.2));
  }

  void expect_mapping(const QuaderFaceTextureCanvas &actual,
                       const QuaderFaceTextureCanvas &expected) {
    for (std::size_t index = 0; index < actual.uv_from_plane.size(); ++index) {
      EXPECT_NEAR(actual.uv_from_plane[index], expected.uv_from_plane[index],
                   0.00001);
    }
    EXPECT_NEAR(actual.uv_offset.x, expected.uv_offset.x, 0.00001);
    EXPECT_NEAR(actual.uv_offset.y, expected.uv_offset.y, 0.00001);
  }

  void expect_outline(const QuaderFaceTextureCanvas &actual,
                       const QuaderFaceTextureCanvas &expected) {
    ASSERT_EQ(actual.outline.size(), expected.outline.size());
    for (std::size_t index = 0; index < actual.outline.size(); ++index) {
      EXPECT_EQ(actual.outline[index].x, expected.outline[index].x);
      EXPECT_EQ(actual.outline[index].y, expected.outline[index].y);
    }
  }

  int argc_ = 1;
  char executable_[32] = "quader-face-texture-tests";
  std::array<char *, 2> argv_{executable_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::optional<QuaderStandaloneEditor> standalone_;
  QuaderModelingObjectRef object_;
  std::unique_ptr<QtFaceTextureCanvas> canvas_;
};

TEST_F(QtFaceTextureCanvasTests,
       SelectionStatesAndUnchangedRefreshRetainTheSingleFacePresentation) {
  ASSERT_EQ(canvas_->presentation().selected_face_count, 1u);
  ASSERT_GE(canvas_->presentation().outline.size(), 3u);
  const auto *retained_outline = canvas_->presentation().outline.data();
  const ::QTransform view = canvas_->plane_to_view();
  canvas_->setToolTip(QStringLiteral("unchanged presentation sentinel"));
  for (int frame = 0; frame < 30; ++frame) {
    canvas_->refresh();
  }
  EXPECT_EQ(canvas_->presentation().outline.data(), retained_outline);
  EXPECT_EQ(canvas_->toolTip(), QStringLiteral("unchanged presentation sentinel"));
  EXPECT_EQ(canvas_->plane_to_view(), view);

  ASSERT_TRUE(editor().selection().select_faces(object_, {1, 2}).ok());
  canvas_->refresh();
  EXPECT_EQ(canvas_->presentation().selected_face_count, 2u);
  EXPECT_FALSE(canvas_->presentation().editable);
  EXPECT_TRUE(canvas_->presentation().outline.empty());
  ASSERT_TRUE(editor().selection().clear().ok());
  canvas_->refresh();
  EXPECT_EQ(canvas_->presentation().selected_face_count, 0u);
}

TEST_F(QtFaceTextureCanvasTests,
       BackgroundDraggingKeepsOutlineFixedAndCommitsOneUndoablePreview) {
  ASSERT_TRUE(canvas_->presentation().editable)
      << canvas_->presentation().disabled_reason;
  const auto original = canvas_->presentation();
  const ::QTransform view = canvas_->plane_to_view();
  const QPointF start = background_location();
  mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(canvas_->interaction_active());
  mouse(QEvent::MouseMove, start + QPointF(25, 13), Qt::NoButton,
         Qt::LeftButton);
  EXPECT_TRUE(canvas_->presentation().gesture_active);
  expect_outline(canvas_->presentation(), original);
  EXPECT_EQ(canvas_->plane_to_view(), view);
  const auto preview = canvas_->presentation();
  mouse(QEvent::MouseButtonRelease, start + QPointF(25, 13), Qt::LeftButton,
         Qt::NoButton);
  EXPECT_FALSE(canvas_->interaction_active());
  expect_mapping(canvas_->presentation(), preview);
  ASSERT_TRUE(editor().history().undo().ok());
  canvas_->refresh();
  expect_mapping(canvas_->presentation(), original);
  ASSERT_TRUE(editor().history().redo().ok());
  canvas_->refresh();
  expect_mapping(canvas_->presentation(), preview);
}

TEST_F(QtFaceTextureCanvasTests,
       OppositeEdgeScaleHandlesControlHeightAndWidthWithPrecisionModifiers) {
  ASSERT_TRUE(canvas_->presentation().editable);
  for (const bool height : {true, false}) {
    SCOPED_TRACE(height ? "upper X handle scales height" :
                            "right Y handle scales width");
    const auto before = canvas_->presentation();
    const QRectF bounds = plane_bounds();
    const QPointF plane_handle =
        height ? QPointF(bounds.center().x(), bounds.bottom())
               : QPointF(bounds.right(), bounds.center().y());
    const QPointF start = canvas_->plane_to_view().map(plane_handle);
    const QPointF pivot = canvas_->plane_to_view().map(
        height ? QPointF(bounds.center().x(), bounds.top())
               : QPointF(bounds.left(), bounds.center().y()));
    // Default repeats are two map units: a small pixel movement can correctly
    // quantize back to two. Cross a complete precision step before checking undo.
    const QPointF end = start + (start - pivot) * 0.625;
    mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    ASSERT_TRUE(canvas_->interaction_active());
    mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
           Qt::ControlModifier);
    mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
           Qt::ControlModifier);
    const auto after = canvas_->presentation();
    const double changed = height ? after.repeat_height : after.repeat_width;
    EXPECT_NEAR(changed, std::round(changed), 0.0001);
    ASSERT_GT(changed, height ? before.repeat_height : before.repeat_width);
    EXPECT_NEAR(height ? after.repeat_width : after.repeat_height,
                 height ? before.repeat_width : before.repeat_height, 0.0001);
    expect_outline(after, before);
    ASSERT_TRUE(editor().history().undo().ok());
    canvas_->refresh();
    expect_mapping(canvas_->presentation(), before);
  }
}

TEST_F(QtFaceTextureCanvasTests,
       CentralRotationClicksAreNoOpsAndDraggingKeepsThePivotFixed) {
  const auto before = canvas_->presentation();
  const auto history = editor().history().snapshot();
  const QPointF pivot = plane_bounds().center();
  const QPointF center = canvas_->plane_to_view().map(pivot);
  for (const QPointF offset : {QPointF{}, QPointF(1, 1)}) {
    mouse(QEvent::MouseButtonPress, center + offset, Qt::LeftButton,
           Qt::LeftButton);
    ASSERT_TRUE(canvas_->interaction_active());
    mouse(QEvent::MouseButtonRelease, center + offset, Qt::LeftButton,
           Qt::NoButton);
    expect_mapping(canvas_->presentation(), before);
    EXPECT_EQ(editor().history().snapshot().undo_label, history.undo_label);
  }
  mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  mouse(QEvent::MouseMove, center + QPointF(35, -30), Qt::NoButton,
         Qt::LeftButton, Qt::ControlModifier);
  mouse(QEvent::MouseButtonRelease, center + QPointF(35, -30),
         Qt::LeftButton, Qt::NoButton, Qt::ControlModifier);
  const auto after = canvas_->presentation();
  const auto sample_pivot = [pivot](const QuaderFaceTextureCanvas &snapshot) {
    const auto &map = snapshot.uv_from_plane;
    return QPointF(map[0] * pivot.x() + map[1] * pivot.y() + snapshot.uv_offset.x,
                    map[2] * pivot.x() + map[3] * pivot.y() + snapshot.uv_offset.y);
  };
  EXPECT_NE(after.uv_from_plane, before.uv_from_plane);
  EXPECT_NEAR(sample_pivot(after).x(), sample_pivot(before).x(), 0.0001);
  EXPECT_NEAR(sample_pivot(after).y(), sample_pivot(before).y(), 0.0001);
  expect_outline(after, before);
  ASSERT_TRUE(editor().history().undo().ok());
  canvas_->refresh();
  expect_mapping(canvas_->presentation(), before);
}

TEST_F(QtFaceTextureCanvasTests,
       EscapeAndCaptureLossCancelAndTrailingReleaseDoesNotCommit) {
  const auto before = canvas_->presentation();
  const QPointF start = background_location();
  const auto history = editor().history().snapshot();
  for (const bool escape : {true, false}) {
    mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, start + QPointF(35, 17), Qt::NoButton,
           Qt::LeftButton);
    ASSERT_TRUE(canvas_->interaction_active());
    if (escape) {
      QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(canvas_.get(), &event);
    } else {
      QEvent event(QEvent::UngrabMouse);
      QCoreApplication::sendEvent(canvas_.get(), &event);
    }
    EXPECT_FALSE(canvas_->interaction_active());
    mouse(QEvent::MouseButtonRelease, start + QPointF(35, 17),
           Qt::LeftButton, Qt::NoButton);
    expect_mapping(canvas_->presentation(), before);
    EXPECT_EQ(editor().history().snapshot().undo_label, history.undo_label);
  }
}

TEST_F(QtFaceTextureCanvasTests, PanAndCursorZoomOnlyChangeTheView) {
  const auto before = canvas_->presentation();
  const QPointF start(90, 80);
  const ::QTransform original_view = canvas_->plane_to_view();
  mouse(QEvent::MouseButtonPress, start, Qt::MiddleButton, Qt::MiddleButton);
  mouse(QEvent::MouseMove, start + QPointF(20, 14), Qt::NoButton,
         Qt::MiddleButton);
  mouse(QEvent::MouseButtonRelease, start + QPointF(20, 14),
         Qt::MiddleButton, Qt::NoButton);
  EXPECT_NE(canvas_->plane_to_view(), original_view);
  const QPointF fixed_plane = canvas_->plane_to_view().inverted().map(start);
  QWheelEvent wheel(start, canvas_->mapToGlobal(start.toPoint()), {},
                     QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                     Qt::NoScrollPhase, false);
  QCoreApplication::sendEvent(canvas_.get(), &wheel);
  const QPointF after_plane = canvas_->plane_to_view().inverted().map(start);
  EXPECT_NEAR(after_plane.x(), fixed_plane.x(), 0.00001);
  EXPECT_NEAR(after_plane.y(), fixed_plane.y(), 0.00001);
  expect_mapping(canvas_->presentation(), before);
  EXPECT_FALSE(canvas_->presentation().gesture_active);
}

TEST_F(QtFaceTextureCanvasTests, SamplingCoordinatesMatchRasterRowsAndRepeat) {
  ASSERT_TRUE(editor().modeling().apply_face_texture_action({
      .action = QuaderFaceTextureAction::FitBoth,
      .selection_token = editor().modeling().face_texture_settings().selection_token,
  }).ok());
  canvas_->refresh();
  // Flat quadrants keep each sample away from bilinear reconstruction borders;
  // a four-texel source produces legitimate channel rounding at projected centers.
  const std::array<QColor, 4> colors{QColor(Qt::red), QColor(Qt::green),
                                     QColor(Qt::blue), QColor(Qt::yellow)};
  QImage source(64, 64, QImage::Format_RGB32);
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      source.setPixelColor(x, y, colors[(y / 32) * 2 + x / 32]);
    }
  }
  QByteArray encoded;
  QBuffer buffer(&encoded);
  ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
  ASSERT_TRUE(source.save(&buffer, "PNG"));
  QtMaterialPresentationItem material;
  material.reference = canvas_->material_reference();
  material.encoded_albedo = encoded;
  material.has_valid_albedo = true;
  material.content_revision = 1;
  canvas_->set_material_presentation(material);

  const auto &snapshot = canvas_->presentation();
  const auto &map = snapshot.uv_from_plane;
  const ::QTransform plane_to_uv(map[0], map[2], map[1], map[3],
                                snapshot.uv_offset.x, snapshot.uv_offset.y);
  const ::QTransform sampling_to_view =
      plane_to_uv.inverted() * canvas_->plane_to_view();
  QImage output(canvas_->size(), QImage::Format_ARGB32);
  canvas_->render(&output);
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 2; ++column) {
      const QPoint pixel = sampling_to_view.map(
          QPointF(column * 0.5 + 0.25, row * 0.5 + 0.25)).toPoint();
      ASSERT_TRUE(output.rect().contains(pixel));
      EXPECT_EQ(output.pixelColor(pixel), colors[row * 2 + column]);
    }
  }
}

TEST_F(QtFaceTextureCanvasTests,
       CompositeMovesControlsOutOfToolSettingsAndKeepsBrowserWithNoFaces) {
  QtFaceTexturePanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen);
  panel.bind(editor());
  panel.show();
  QCoreApplication::processEvents();
  QtToolSettingsPanel tool_settings;
  tool_settings.bind(editor());
  EXPECT_NE(panel.findChild<QDoubleSpinBox *>(
                QStringLiteral("face_texture.texture.scale_x")), nullptr);
  EXPECT_EQ(tool_settings.findChild<QDoubleSpinBox *>(
                QStringLiteral("face_texture.texture.scale_x")), nullptr);
  EXPECT_EQ(panel.browser().purpose(), QtMaterialBrowserPurpose::FaceAssignment);
  ASSERT_TRUE(editor().selection().clear().ok());
  panel.refresh();
  EXPECT_FALSE(panel.browser().isHidden());
  const QByteArray split = panel.save_splitter_state();
  EXPECT_FALSE(split.isEmpty());
  EXPECT_TRUE(panel.restore_splitter_state(split));
  EXPECT_TRUE(qt_object_belongs_to_face_texture_panel(&panel.canvas()));
  panel.abandon();
  tool_settings.unbind();
}

TEST_F(QtFaceTextureCanvasTests,
       CompactControlsFitNormalDockAndScrollWhenSpaceIsRestricted) {
  QtFaceTexturePanel panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen);
  panel.resize(360, 800);
  panel.bind(editor());
  panel.show();
  QCoreApplication::processEvents();
  auto *scroll = panel.findChild<QScrollArea *>(
      QStringLiteral("face_texture.controls_scroll"));
  ASSERT_NE(scroll, nullptr);
  EXPECT_EQ(scroll->verticalScrollBar()->maximum(), 0);
  EXPECT_EQ(scroll->horizontalScrollBar()->maximum(), 0);
  EXPECT_GE(panel.canvas().height(), panel.canvas().minimumHeight());
  EXPECT_GE(panel.browser().height(), panel.browser().minimumHeight());

  panel.apply_ui_scale(QtUiScaleMetrics(2.0F, 1));
  panel.resize(240, 420);
  QCoreApplication::processEvents();
  EXPECT_GT(scroll->verticalScrollBar()->maximum(), 0);
  EXPECT_GE(scroll->widget()->height(), scroll->viewport()->height());
  panel.abandon();
}

TEST_F(QtFaceTextureCanvasTests, RuntimeAbandonmentDoesNotAccessDestroyedOwner) {
  const QPointF start = background_location();
  mouse(QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton);
  mouse(QEvent::MouseMove, start + QPointF(10, 10), Qt::NoButton,
         Qt::LeftButton);
  ASSERT_TRUE(canvas_->interaction_active());
  standalone_.reset();
  canvas_->abandon();
  EXPECT_FALSE(canvas_->interaction_active());
  mouse(QEvent::MouseButtonRelease, start, Qt::LeftButton, Qt::NoButton);
  canvas_->refresh();
  EXPECT_EQ(canvas_->presentation().selected_face_count, 0u);
}

TEST_F(QtFaceTextureCanvasTests,
       AlbedoTintUsesLinearMaterialColorAndPreservesSharedPreview) {
  QImage source(1, 1, QImage::Format_RGB32);
  source.fill(Qt::white);
  QByteArray encoded;
  QBuffer buffer(&encoded);
  ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
  ASSERT_TRUE(source.save(&buffer, "PNG"));
  QtMaterialPresentationItem material;
  material.reference = canvas_->material_reference();
  material.encoded_albedo = encoded;
  material.has_valid_albedo = true;
  material.content_revision = 1;
  material.fallback_color = QColor::fromRgbF(0.25, 0.5, 1.0);
  material.thumbnail = make_qt_material_thumbnail(material);
  canvas_->set_material_presentation(material);

  const auto expect_background = [this](QColor expected) {
    QImage output(canvas_->size(), QImage::Format_ARGB32);
    canvas_->render(&output);
    const QColor actual = output.pixelColor(1, 1);
    EXPECT_NEAR(actual.red(), expected.red(), 1);
    EXPECT_NEAR(actual.green(), expected.green(), 1);
    EXPECT_NEAR(actual.blue(), expected.blue(), 1);
    EXPECT_EQ(actual.alpha(), 255);
  };
  // Linear 0.25 and 0.5 display as approximately sRGB 137 and 188.
  expect_background(QColor(137, 188, 255));
  EXPECT_EQ(material.thumbnail->image.pixelColor(0, 0), QColor(Qt::white));

  material.fallback_color = QColor::fromRgbF(0.5, 0.25, 1.0);
  canvas_->set_material_presentation(material);
  expect_background(QColor(188, 137, 255));

  source.fill(QColor(128, 128, 128));
  encoded.clear();
  buffer.seek(0);
  ASSERT_TRUE(source.save(&buffer, "PNG"));
  material.encoded_albedo = encoded;
  material.thumbnail.reset();
  material.content_revision = 2;
  material.fallback_color = QColor::fromRgbF(0.5, 0.5, 0.5);
  canvas_->set_material_presentation(material);
  // A mid-gray sRGB texel multiplied by linear 0.5 is sRGB 92, not 64.
  expect_background(QColor(92, 92, 92));

  material.encoded_albedo.clear();
  material.has_valid_albedo = false;
  material.content_revision = 3;
  material.fallback_color = QColor::fromRgbF(0.25, 0.5, 1.0);
  canvas_->set_material_presentation(material);
  expect_background(QColor(137, 188, 255));
}

} // namespace
} // namespace quader
