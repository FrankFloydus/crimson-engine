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
#include "ui/qt_numeric_format.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_tool_settings_panel.hpp"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegion>
#include <QScrollArea>
#include <QScrollBar>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

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
    ASSERT_TRUE(editor().selection().select_object(object_).ok());
    ASSERT_TRUE(editor().mode().set(QuaderEditorMode::Edit).ok());
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

  std::optional<QPointF> painted_scale_handle(bool width) const {
    const QImage image = render_canvas_surface();
    const QRgb color = qt_semantic_axis_color(
        width ? quader_editor_core::ViewAxis::X
              : quader_editor_core::ViewAxis::Y).rgb();
    QRect colored_pixels;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (image.pixel(x, y) == color) {
          colored_pixels = colored_pixels.united(QRect(x, y, 1, 1));
        }
      }
    }
    return colored_pixels.isEmpty()
        ? std::nullopt
        : std::optional<QPointF>{QRectF(colored_pixels).center()};
  }

  QImage render_canvas_surface() const {
    QImage image(canvas_->size(), QImage::Format_ARGB32);
    canvas_->render(&image, QPoint(), QRegion(), QWidget::DrawWindowBackground);
    return image;
  }

  void set_directional_brick_texture() {
    QImage source(64, 64, QImage::Format_RGB32);
    source.fill(QColor(30, 33, 37));
    QPainter painter(&source);
    painter.setPen(QColor(117, 120, 121));
    for (int row = 0; row < 4; ++row) {
      const int offset = row % 2 == 0 ? 0 : -16;
      for (int column = 0; column < 3; ++column) {
        const QRect brick(offset + column * 32 + 1, row * 16 + 1, 30, 14);
        painter.fillRect(brick, QColor(69 + row * 5, 73 + row * 5, 78 + row * 5));
        painter.drawLine(brick.topLeft(), brick.topRight());
        painter.drawLine(brick.topLeft(), brick.bottomLeft());
      }
    }
    painter.end();
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

  void save_image(const QString &name, const QImage &image) const {
    const QString path =
        qEnvironmentVariable("QUADER_FACE_TEXTURE_SCREENSHOT_DIR");
    if (path.isEmpty()) {
      return;
    }
    const QDir directory(path);
    ASSERT_TRUE(directory.mkpath(QStringLiteral(".")));
    EXPECT_TRUE(image.save(directory.filePath(name), "PNG"));
  }

  void save_canvas(const QString &name) const {
    if (qEnvironmentVariable("QUADER_FACE_TEXTURE_SCREENSHOT_DIR").isEmpty()) {
      return;
    }
    QImage image(canvas_->size(), QImage::Format_ARGB32);
    canvas_->render(&image);
    save_image(name, image);
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
    SCOPED_TRACE(height ? "green V handle scales height" :
                            "red U handle scales width");
    const auto before = canvas_->presentation();
    const QRectF bounds = plane_bounds();
    const auto painted = painted_scale_handle(!height);
    ASSERT_TRUE(painted.has_value());
    const QPointF start = *painted;
    const QPointF pivot = canvas_->plane_to_view().map(bounds.center()) * 2.0 - start;
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
       PaintedScaleHandlesFollowSignedTextureAxesAcrossFacesAndRotatedMirrors) {
  // Preserve authored cube projections: forcing Rotate=0 would erase the
  // differing texture directions on side faces that prompted this regression.
  for (int scenario = 0; scenario < 8; ++scenario) {
    const QuaderElementId face = scenario < 6 ? scenario + 1 : 2;
    SCOPED_TRACE(::testing::Message() << "face=" << face
                                    << " scenario=" << scenario);
    ASSERT_TRUE(editor().selection().select_face(object_, face).ok());
    if (scenario >= 6) {
      for (const auto [field, value] : {
          std::pair{QuaderFaceTextureField::ScaleX, scenario == 6 ? -8.0 : 8.0},
          std::pair{QuaderFaceTextureField::ScaleY, scenario == 6 ? 16.0 : -16.0},
          std::pair{QuaderFaceTextureField::Rotate, scenario == 6 ? 90.0 : 37.0}}) {
        ASSERT_TRUE(editor().modeling().set_face_texture_setting({
            .field = field, .value = value,
            .selection_token = editor().modeling()
                                   .face_texture_settings().selection_token}).ok());
      }
    }
    canvas_->refresh();
    ASSERT_TRUE(canvas_->presentation().editable);
    save_canvas(QStringLiteral("canvas_texture_axes_%1.png").arg(scenario));
    for (const bool width : {true, false}) {
      SCOPED_TRACE(width ? "red U width" : "green V height");
      const auto before = canvas_->presentation();
      const auto stored_before = editor().modeling().face_texture_settings();
      const auto &mapping = before.uv_from_plane;
      const double determinant = mapping[0] * mapping[3] - mapping[1] * mapping[2];
      ASSERT_GT(std::abs(determinant), 1.0e-12);
      QPointF direction = width
          ? QPointF(mapping[3], -mapping[2]) / determinant
          : QPointF(-mapping[1], mapping[0]) / determinant;
      direction /= std::hypot(direction.x(), direction.y());
      const QRectF bounds = plane_bounds();
      const QPointF center = bounds.center();
      const QPointF center_view = canvas_->plane_to_view().map(center);
      const auto painted = painted_scale_handle(width);
      ASSERT_TRUE(painted.has_value());
      const QPointF painted_plane = canvas_->plane_to_view().inverted().map(*painted);
      const QPointF displayed_direction = painted_plane - center;
      const double displayed_length = std::hypot(displayed_direction.x(),
                                                  displayed_direction.y());
      ASSERT_GT(displayed_length, 0.0);
      EXPECT_GT(QPointF::dotProduct(displayed_direction / displayed_length,
                                   direction), 0.999);

      // The opposite face-boundary point is the pinned UV pivot, independently
      // of where rasterization rounded the colored handle's center.
      const double reach = std::min(
          std::abs(direction.x()) > 1.0e-10
              ? bounds.width() / (2.0 * std::abs(direction.x())) : 1.0e30,
          std::abs(direction.y()) > 1.0e-10
              ? bounds.height() / (2.0 * std::abs(direction.y())) : 1.0e30);
      const QPointF pivot = center - direction * reach;
      const auto uv_at = [](const QuaderFaceTextureCanvas &state, QPointF point) {
        const auto &matrix = state.uv_from_plane;
        return QPointF(matrix[0] * point.x() + matrix[1] * point.y() + state.uv_offset.x,
                       matrix[2] * point.x() + matrix[3] * point.y() + state.uv_offset.y);
      };
      for (const bool outward : {true, false}) {
        SCOPED_TRACE(outward ? "outward commit and undo" : "inward cancel");
        const QPointF end = *painted + (*painted - center_view) *
            (outward ? 1.25 : -1.0);
        mouse(QEvent::MouseButtonPress, *painted, Qt::LeftButton, Qt::LeftButton);
        ASSERT_TRUE(canvas_->interaction_active());
        mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
              Qt::ControlModifier);
        const auto after = canvas_->presentation();
        const double repeat_before = width ? before.repeat_width : before.repeat_height;
        const double repeat_after = width ? after.repeat_width : after.repeat_height;
        if (outward) {
          EXPECT_GT(repeat_after, repeat_before);
        } else {
          EXPECT_LT(repeat_after, repeat_before);
        }
        EXPECT_NEAR(width ? after.repeat_height : after.repeat_width,
                    width ? before.repeat_height : before.repeat_width, 0.0001);
        const auto stored_after = editor().modeling().face_texture_settings();
        ASSERT_TRUE(stored_before.scale_x.has_value());
        ASSERT_TRUE(stored_before.scale_y.has_value());
        ASSERT_TRUE(stored_after.scale_x.has_value());
        ASSERT_TRUE(stored_after.scale_y.has_value());
        EXPECT_EQ(std::signbit(*stored_after.scale_x), std::signbit(*stored_before.scale_x));
        EXPECT_EQ(std::signbit(*stored_after.scale_y), std::signbit(*stored_before.scale_y));
        EXPECT_NEAR(uv_at(after, pivot).x(), uv_at(before, pivot).x(), 0.0001);
        EXPECT_NEAR(uv_at(after, pivot).y(), uv_at(before, pivot).y(), 0.0001);
        expect_outline(after, before);
        if (outward) {
          mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton,
                Qt::ControlModifier);
          ASSERT_TRUE(editor().history().undo().ok());
          canvas_->refresh();
        } else {
          QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
          QCoreApplication::sendEvent(canvas_.get(), &escape);
          mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
        }
        EXPECT_FALSE(canvas_->interaction_active());
        expect_mapping(canvas_->presentation(), before);
        expect_outline(canvas_->presentation(), before);
      }
    }
  }
}

TEST_F(QtFaceTextureCanvasTests,
       TextureAxisBadgesExplainWidthAndHeightAndRevealScaleGuidesOnlyWhenEditable) {
  auto *feedback = canvas_->findChild<QLabel *>(
      QStringLiteral("face_texture.canvas.measurement"));
  ASSERT_NE(feedback, nullptr);
  for (const bool rotated_mirror : {false, true}) {
    SCOPED_TRACE(rotated_mirror ? "oblique mirrored texture" : "authored face");
    for (const auto [field, value] : {
         std::pair{QuaderFaceTextureField::ScaleX, 32.0},
         std::pair{QuaderFaceTextureField::ScaleY, rotated_mirror ? -32.0 : 32.0}}) {
      ASSERT_TRUE(editor().modeling().set_face_texture_setting({
          .field = field, .value = value,
          .selection_token = editor().modeling()
                                 .face_texture_settings().selection_token}).ok());
    }
    if (rotated_mirror) {
      ASSERT_TRUE(editor().modeling().set_face_texture_setting({
          .field = QuaderFaceTextureField::Rotate, .value = 37.0,
          .selection_token = editor().modeling()
                                 .face_texture_settings().selection_token}).ok());
    }
    canvas_->resize(rotated_mirror ? QSize(220, 260) : QSize(420, 320));
    canvas_->refresh();
    set_directional_brick_texture();
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(canvas_.get(), &leave);
    const QString scenario = rotated_mirror ? QStringLiteral("mirrored")
                                            : QStringLiteral("authored");
    save_canvas(QStringLiteral("canvas_uv_%1_idle.png").arg(scenario));
    const QImage idle = render_canvas_surface();
    const auto before = canvas_->presentation();
    for (const bool width : {true, false}) {
      SCOPED_TRACE(width ? "U width" : "V height");
      const auto handle = painted_scale_handle(width);
      ASSERT_TRUE(handle.has_value());
      const QPoint location = handle->toPoint();
      int contrasting_glyph_pixels = 0;
      for (int y = -4; y <= 4; ++y) {
        for (int x = -4; x <= 4; ++x) {
          const QPoint sample = location + QPoint(x, y);
          ASSERT_TRUE(idle.rect().contains(sample));
          const QColor color = idle.pixelColor(sample);
          const int brightest = std::max({color.red(), color.green(), color.blue()});
          const int darkest = std::min({color.red(), color.green(), color.blue()});
          if (brightest < 65 || darkest > 210) {
            ++contrasting_glyph_pixels;
          }
        }
      }
      // The letter remains a visible identifier without relying on axis color.
      EXPECT_GE(contrasting_glyph_pixels, 3);
      mouse(QEvent::MouseMove, *handle, Qt::NoButton, Qt::NoButton);
      EXPECT_TRUE(canvas_->toolTip().contains(width
          ? QStringLiteral("Texture width (U)")
          : QStringLiteral("Texture height (V)")));
      EXPECT_FALSE(feedback->isHidden());
      EXPECT_TRUE(feedback->accessibleName().contains(QStringLiteral("U width:")));
      EXPECT_TRUE(feedback->accessibleName().contains(QStringLiteral("V height:")));

      const QImage hovered = render_canvas_surface();
      const QRect interior = canvas_->plane_to_view().mapRect(plane_bounds())
                                 .toAlignedRect().adjusted(18, 18, -18, -18)
                                 .intersected(canvas_->rect());
      int changed_interior_pixels = 0;
      for (int y = interior.top(); y <= interior.bottom(); ++y) {
        for (int x = interior.left(); x <= interior.right(); ++x) {
          if (hovered.pixel(x, y) != idle.pixel(x, y)) {
            ++changed_interior_pixels;
          }
        }
      }
      // A texture-aligned guide must reach the face interior; changing just
      // the badge border would leave the scaling direction unexplained.
      EXPECT_GT(changed_interior_pixels, 30);
      const QString axis = width ? QStringLiteral("u") : QStringLiteral("v");
      save_canvas(QStringLiteral("canvas_uv_%1_%2_hover.png").arg(scenario, axis));
      const QPointF center = canvas_->plane_to_view().map(plane_bounds().center());
      const QPointF end = *handle + (*handle - center) * 0.3;
      mouse(QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
      mouse(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton,
            Qt::ControlModifier);
      ASSERT_TRUE(canvas_->interaction_active());
      save_canvas(QStringLiteral("canvas_uv_%1_%2_drag.png").arg(scenario, axis));
      QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
      QCoreApplication::sendEvent(canvas_.get(), &escape);
      mouse(QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton);
      QCoreApplication::sendEvent(canvas_.get(), &leave);
      expect_mapping(canvas_->presentation(), before);
    }
  }

  const auto old_handle = painted_scale_handle(true);
  ASSERT_TRUE(old_handle.has_value());
  mouse(QEvent::MouseMove, *old_handle, Qt::NoButton, Qt::NoButton);
  ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
  canvas_->refresh();
  ASSERT_FALSE(canvas_->presentation().editable);
  EXPECT_TRUE(feedback->isHidden());
  EXPECT_FALSE(painted_scale_handle(true).has_value());
  EXPECT_FALSE(painted_scale_handle(false).has_value());
  const QImage disabled = render_canvas_surface();
  mouse(QEvent::MouseMove, *old_handle, Qt::NoButton, Qt::NoButton);
  EXPECT_EQ(render_canvas_surface(), disabled);
  ASSERT_TRUE(editor().modeling().cancel_transform_preview().ok());
  ASSERT_TRUE(editor().selection().clear().ok());
  canvas_->refresh();
  const QImage empty = render_canvas_surface();
  mouse(QEvent::MouseMove, *old_handle, Qt::NoButton, Qt::NoButton);
  EXPECT_EQ(render_canvas_surface(), empty);
  EXPECT_TRUE(feedback->isHidden());
  EXPECT_FALSE(painted_scale_handle(true).has_value());
  EXPECT_FALSE(painted_scale_handle(false).has_value());
}

TEST_F(QtFaceTextureCanvasTests,
       HoveredTextureAxisDashesReachBothCanvasEdgesAndUseGrabCursors) {
  for (const bool rotated_mirror : {false, true}) {
    SCOPED_TRACE(rotated_mirror ? "oblique mirrored texture" : "authored face");
    if (rotated_mirror) {
      for (const auto [field, value] : {
           std::pair{QuaderFaceTextureField::ScaleY, -32.0},
           std::pair{QuaderFaceTextureField::Rotate, 37.0}}) {
        ASSERT_TRUE(editor().modeling().set_face_texture_setting({
            .field = field, .value = value,
            .selection_token = editor().modeling()
                                   .face_texture_settings().selection_token}).ok());
      }
      canvas_->refresh();
    }
    const QPointF center = canvas_->plane_to_view().map(plane_bounds().center());
    // Leave enough space beyond the face for several visible dashes and gaps.
    QWheelEvent wheel(center, canvas_->mapToGlobal(center.toPoint()), {},
                      QPoint(0, -360), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(canvas_.get(), &wheel);
    set_directional_brick_texture();
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(canvas_.get(), &leave);
    const QImage idle = render_canvas_surface();
    for (const bool width : {true, false}) {
      SCOPED_TRACE(width ? "U width" : "V height");
      const auto handle = painted_scale_handle(width);
      ASSERT_TRUE(handle.has_value());
      const auto &mapping = canvas_->presentation().uv_from_plane;
      const ::QTransform plane_to_uv(mapping[0], mapping[2], mapping[1], mapping[3], 0, 0);
      const ::QTransform uv_to_view = plane_to_uv.inverted() * canvas_->plane_to_view();
      const QPointF axis = uv_to_view.map(width ? QPointF(1, 0) : QPointF(0, 1))
                           - uv_to_view.map(QPointF());
      const QPointF direction = axis / std::hypot(axis.x(), axis.y());
      const QRectF face_margin = canvas_->plane_to_view().mapRect(plane_bounds())
                                     .adjusted(-18, -18, 18, 18);
      const QRect safe_canvas = canvas_->rect().adjusted(2, 2, -2, -2);
      std::array<std::vector<QPoint>, 2> rays;
      for (std::size_t side = 0; side < rays.size(); ++side) {
        for (int distance = 1; distance < canvas_->width() + canvas_->height(); ++distance) {
          const QPointF point = center + direction * distance * (side == 0 ? -1.0 : 1.0);
          if (!safe_canvas.contains(point.toPoint())) {
            break;
          }
          if (!face_margin.contains(point)) {
            rays[side].push_back(point.toPoint());
          }
        }
        ASSERT_GT(rays[side].size(), 24u);
      }
      const auto expect_axis = [&idle, &rays](const QImage &image, bool visible) {
        for (std::size_t side = 0; side < rays.size(); ++side) {
          SCOPED_TRACE(side == 0 ? "opposite ray" : "handle ray");
          int strokes = 0;
          int gaps = 0;
          int strokes_near_edge = 0;
          for (std::size_t index = 0; index < rays[side].size(); ++index) {
            bool changed = false;
            for (int y = -1; y <= 1; ++y) {
              for (int x = -1; x <= 1; ++x) {
                const QPoint sample = rays[side][index] + QPoint(x, y);
                changed = changed || image.pixel(sample) != idle.pixel(sample);
              }
            }
            if (changed) {
              ++strokes;
              if (index + 16 >= rays[side].size()) {
                ++strokes_near_edge;
              }
            } else {
              ++gaps;
            }
          }
          if (visible) {
            EXPECT_GT(strokes, 4);
            EXPECT_GT(gaps, 2);
            EXPECT_GT(strokes_near_edge, 2);
          } else {
            EXPECT_EQ(strokes, 0);
          }
        }
      };
      mouse(QEvent::MouseMove, *handle, Qt::NoButton, Qt::NoButton);
      EXPECT_EQ(canvas_->cursor().shape(), Qt::OpenHandCursor);
      expect_axis(render_canvas_surface(), true);
      save_canvas(QStringLiteral("canvas_axis_guide_%1_%2_hover.png")
                      .arg(rotated_mirror ? QStringLiteral("mirrored") : QStringLiteral("authored"),
                           width ? QStringLiteral("u") : QStringLiteral("v")));
      mouse(QEvent::MouseButtonPress, *handle, Qt::LeftButton, Qt::LeftButton);
      ASSERT_TRUE(canvas_->interaction_active());
      EXPECT_EQ(canvas_->cursor().shape(), Qt::ClosedHandCursor);
      expect_axis(render_canvas_surface(), true);
      mouse(QEvent::MouseButtonRelease, *handle, Qt::LeftButton, Qt::NoButton);
      ASSERT_FALSE(canvas_->interaction_active());
      EXPECT_EQ(canvas_->cursor().shape(), Qt::OpenHandCursor);
      expect_axis(render_canvas_surface(), true);
      QCoreApplication::sendEvent(canvas_.get(), &leave);
      EXPECT_EQ(render_canvas_surface(), idle);

      mouse(QEvent::MouseMove, *handle, Qt::NoButton, Qt::NoButton);
      ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
      canvas_->refresh();
      ASSERT_FALSE(canvas_->presentation().editable);
      expect_axis(render_canvas_surface(), false);
      ASSERT_TRUE(editor().modeling().cancel_transform_preview().ok());
      canvas_->refresh();
      QCoreApplication::sendEvent(canvas_.get(), &leave);
    }
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
       RotationCursorAndAbsoluteAngleFeedbackFollowTheCapturedGesture) {
  ASSERT_TRUE(editor().modeling().set_face_texture_setting({
      .field = QuaderFaceTextureField::Rotate,
      .value = 27.0,
      .selection_token =
          editor().modeling().face_texture_settings().selection_token,
  }).ok());
  canvas_->refresh();
  const auto initial = canvas_->presentation();
  auto *feedback = canvas_->findChild<QLabel *>(
      QStringLiteral("face_texture.canvas.measurement"));
  ASSERT_NE(feedback, nullptr);
  EXPECT_TRUE(feedback->isHidden());
  const QPointF center = canvas_->plane_to_view().map(plane_bounds().center());
  mouse(QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
  ASSERT_EQ(canvas_->cursor().shape(), Qt::BitmapCursor);
  const QPixmap rotation_cursor = canvas_->cursor().pixmap();
  ASSERT_FALSE(rotation_cursor.isNull());
  EXPECT_TRUE(rotation_cursor.hasAlphaChannel());
  mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
  ASSERT_TRUE(canvas_->interaction_active());
  EXPECT_FALSE(feedback->isHidden());
  EXPECT_TRUE(feedback->testAttribute(Qt::WA_TransparentForMouseEvents));
  EXPECT_EQ(feedback->text(),
            QStringLiteral("Rotation: %1°")
                .arg(qt_format_number(initial.rotation_degrees,
                                       canvas_->locale())));
  mouse(QEvent::MouseMove, center + QPointF(30, -25), Qt::NoButton,
         Qt::LeftButton, Qt::ControlModifier);
  EXPECT_NE(canvas_->presentation().rotation_degrees, initial.rotation_degrees);
  EXPECT_EQ(feedback->text(),
            QStringLiteral("Rotation: %1°")
                .arg(qt_format_number(canvas_->presentation().rotation_degrees,
                                       canvas_->locale())));
  EXPECT_EQ(canvas_->cursor().shape(), Qt::BitmapCursor);
  EXPECT_EQ(canvas_->cursor().pixmap().cacheKey(), rotation_cursor.cacheKey());
  save_canvas(QStringLiteral("canvas_absolute_rotation.png"));
  const auto *retained_outline = canvas_->presentation().outline.data();
  const QRect feedback_geometry = feedback->geometry();
  for (int frame = 0; frame < 30; ++frame) {
    canvas_->refresh();
  }
  EXPECT_EQ(canvas_->presentation().outline.data(), retained_outline);
  EXPECT_EQ(feedback->geometry(), feedback_geometry);
  EXPECT_EQ(canvas_->cursor().pixmap().cacheKey(), rotation_cursor.cacheKey());
  QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
  QCoreApplication::sendEvent(canvas_.get(), &escape);
  EXPECT_FALSE(canvas_->interaction_active());
  EXPECT_TRUE(feedback->isHidden());
  expect_mapping(canvas_->presentation(), initial);
}

TEST_F(QtFaceTextureCanvasTests,
       RotationCursorScalesAndMeasurementBadgesDoNotInterceptTheCanvas) {
  auto *feedback = canvas_->findChild<QLabel *>(
      QStringLiteral("face_texture.canvas.measurement"));
  ASSERT_NE(feedback, nullptr);
  for (const float factor : {1.0f, 1.5f, 2.0f}) {
    SCOPED_TRACE(factor);
    canvas_->apply_ui_scale(QtUiScaleMetrics(factor, 1));
    const QRectF bounds = plane_bounds();
    const QPointF center = canvas_->plane_to_view().map(bounds.center());
    mouse(QEvent::MouseMove, center, Qt::NoButton, Qt::NoButton);
    const QPixmap cursor = canvas_->cursor().pixmap();
    ASSERT_FALSE(cursor.isNull());
    EXPECT_NEAR(cursor.width() / cursor.devicePixelRatioF(), 32.0 * factor, 1.0);
    EXPECT_NEAR(canvas_->cursor().hotSpot().x(), 16.0 * factor, 1.0);
    EXPECT_NEAR(canvas_->cursor().hotSpot().y(), 16.0 * factor, 1.0);
    const int percent = static_cast<int>(std::lround(factor * 100.0f));
    save_image(QStringLiteral("canvas_rotation_cursor_%1.png").arg(percent),
                cursor.toImage());
    mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, center + QPointF(30, -25), Qt::NoButton,
           Qt::LeftButton, Qt::ControlModifier);
    ASSERT_TRUE(canvas_->interaction_active());
    save_canvas(QStringLiteral("canvas_rotation_feedback_%1.png").arg(percent));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas_.get(), &escape);
    ASSERT_FALSE(canvas_->interaction_active());
    const auto scale = painted_scale_handle(true);
    ASSERT_TRUE(scale.has_value());
    mouse(QEvent::MouseMove, *scale, Qt::NoButton, Qt::NoButton);
    EXPECT_EQ(canvas_->cursor().shape(), Qt::OpenHandCursor);
    EXPECT_FALSE(feedback->isHidden());
    EXPECT_EQ(feedback->text(),
              QStringLiteral("U width: %1   V height: %2 map units")
                  .arg(qt_format_number(canvas_->presentation().repeat_width,
                                         canvas_->locale()),
                       qt_format_number(canvas_->presentation().repeat_height,
                                         canvas_->locale())));
    EXPECT_TRUE(feedback->testAttribute(Qt::WA_TransparentForMouseEvents));
    EXPECT_TRUE(canvas_->rect().contains(feedback->geometry()));
    save_canvas(QStringLiteral("canvas_scale_feedback_%1.png").arg(percent));
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(canvas_.get(), &leave);
    EXPECT_TRUE(feedback->isHidden());
  }
}

TEST_F(QtFaceTextureCanvasTests,
       ExistingMeasurementHidesWhenAnotherPreviewDisablesTheSameFace) {
  const auto handle = painted_scale_handle(true);
  ASSERT_TRUE(handle.has_value());
  mouse(QEvent::MouseMove, *handle, Qt::NoButton, Qt::NoButton);
  auto *feedback = canvas_->findChild<QLabel *>(
      QStringLiteral("face_texture.canvas.measurement"));
  ASSERT_NE(feedback, nullptr);
  ASSERT_FALSE(feedback->isHidden());
  const auto original = canvas_->presentation();
  ASSERT_TRUE(editor().modeling().begin_transform_preview().ok());
  canvas_->refresh();
  EXPECT_EQ(canvas_->presentation().object_id, original.object_id);
  EXPECT_EQ(canvas_->presentation().face_id, original.face_id);
  EXPECT_EQ(canvas_->presentation().selected_face_count, 1u);
  EXPECT_FALSE(canvas_->presentation().editable);
  EXPECT_TRUE(feedback->isHidden());
  ASSERT_TRUE(editor().modeling().cancel_transform_preview().ok());
  canvas_->refresh();
  EXPECT_TRUE(canvas_->presentation().editable);
}

TEST_F(QtFaceTextureCanvasTests,
       NarrowFeedbackPreservesLargeValuesAndFitsScaledFontsInsideTheCanvas) {
  for (const auto [field, value] :
       {std::pair{QuaderFaceTextureField::ScaleX, 123456.789},
        std::pair{QuaderFaceTextureField::ScaleY, 987654.321}}) {
    ASSERT_TRUE(editor().modeling().set_face_texture_setting({
        .field = field,
        .value = value,
        .selection_token =
            editor().modeling().face_texture_settings().selection_token,
    }).ok());
  }
  canvas_->refresh();
  ASSERT_GT(canvas_->presentation().repeat_width, 100000.0);
  auto *feedback = canvas_->findChild<QLabel *>(
      QStringLiteral("face_texture.canvas.measurement"));
  ASSERT_NE(feedback, nullptr);
  const auto without_whitespace = [](const QString &text) {
    QString result;
    for (const QChar character : text) {
      if (!character.isSpace()) {
        result += character;
      }
    }
    return result;
  };
  const auto expect_inside = [this, feedback, &without_whitespace](
                                 const QString &complete_text,
                                 const QStringList &numeric_tokens) {
    EXPECT_FALSE(feedback->isHidden());
    EXPECT_TRUE(canvas_->rect().contains(feedback->geometry()));
    EXPECT_LE(feedback->height(),
              static_cast<int>(std::floor(canvas_->height() * 0.35)));
    EXPECT_EQ(feedback->accessibleName(), complete_text);
    EXPECT_EQ(without_whitespace(feedback->text()),
              without_whitespace(complete_text));
    const auto lines = feedback->text().split(QLatin1Char('\n'));
    for (const auto &token : numeric_tokens) {
      EXPECT_TRUE(std::any_of(lines.cbegin(), lines.cend(),
                             [&token](const QString &line) {
                               return line.contains(token);
                             })) << "Split numeric value: " << token.toStdString();
    }
    const QFontMetrics font_metrics(feedback->font());
    for (const auto &line : lines) {
      EXPECT_LE(font_metrics.horizontalAdvance(line),
                feedback->width() - feedback->margin() * 2);
    }
    EXPECT_LE(font_metrics.height() +
                  (lines.size() - 1) * font_metrics.lineSpacing(),
              feedback->height() - feedback->margin() * 2);
  };
  for (const float factor : {1.0f, 1.5f, 2.0f}) {
    SCOPED_TRACE(factor);
    const QtUiScaleMetrics metrics(factor, 2);
    canvas_->apply_ui_scale(metrics);
    canvas_->resize(metrics.size(QSize(96, 128)));
    QFont enlarged = canvas_->font();
    enlarged.setPixelSize(metrics.px(28));
    canvas_->setFont(enlarged);
    const QRectF bounds = plane_bounds();
    const auto scale = painted_scale_handle(true);
    ASSERT_TRUE(scale.has_value());
    mouse(QEvent::MouseMove, *scale, Qt::NoButton, Qt::NoButton);
    const QString repeat_width = qt_format_number(
        canvas_->presentation().repeat_width, canvas_->locale());
    const QString repeat_height = qt_format_number(
        canvas_->presentation().repeat_height, canvas_->locale());
    const QString complete = QStringLiteral("U width: %1   V height: %2 map units")
                                 .arg(repeat_width, repeat_height);
    expect_inside(complete, {repeat_width, repeat_height});
    EXPECT_FALSE(feedback->geometry().contains(scale->toPoint()));
    EXPECT_TRUE(feedback->text().contains(QLatin1Char('\n')));
    const QRect geometry = feedback->geometry();
    const QString text = feedback->text();
    const QFont font = feedback->font();
    for (int frame = 0; frame < 20; ++frame) {
      canvas_->refresh();
    }
    EXPECT_EQ(feedback->geometry(), geometry);
    EXPECT_EQ(feedback->text(), text);
    EXPECT_EQ(feedback->font(), font);
    const int percent = static_cast<int>(std::lround(factor * 100.0f));
    save_canvas(QStringLiteral("canvas_narrow_scale_%1.png").arg(percent));

    const QPointF center = canvas_->plane_to_view().map(bounds.center());
    mouse(QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, center + QPointF(10, -8), Qt::NoButton,
           Qt::LeftButton, Qt::ControlModifier);
    ASSERT_TRUE(canvas_->interaction_active());
    const QString rotation = qt_format_number(
        canvas_->presentation().rotation_degrees, canvas_->locale());
    expect_inside(QStringLiteral("Rotation: %1°").arg(rotation), {rotation});
    EXPECT_FALSE(feedback->geometry().contains(center.toPoint()));
    save_canvas(QStringLiteral("canvas_narrow_rotation_%1.png").arg(percent));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(canvas_.get(), &escape);
    EXPECT_TRUE(feedback->isHidden());
  }
}

TEST_F(QtFaceTextureCanvasTests,
       FaceOutlineRetainsABroadWhiteCoreAndOutsideContrastAcrossUiScales) {
  for (const float factor : {1.0f, 1.5f, 2.0f}) {
    canvas_->apply_ui_scale(QtUiScaleMetrics(factor, 1));
    for (const QColor background : {QColor(Qt::black), QColor(Qt::white)}) {
      SCOPED_TRACE(factor);
      SCOPED_TRACE(background.name().toStdString());
      QtMaterialPresentationItem material;
      material.reference = canvas_->material_reference();
      material.fallback_color = background;
      material.content_revision = 1;
      canvas_->set_material_presentation(material);
      QImage output(canvas_->size(), QImage::Format_ARGB32);
      canvas_->render(&output);
      save_image(QStringLiteral("canvas_outline_%1_%2.png")
                     .arg(background == QColor(Qt::black)
                              ? QStringLiteral("dark")
                              : QStringLiteral("light"))
                     .arg(static_cast<int>(std::lround(factor * 100.0f))),
                   output);
      const QRectF bounds = plane_bounds();
      const QPointF edge = canvas_->plane_to_view().map(QPointF(
          bounds.left() + bounds.width() * 0.25, bounds.top()));
      int white_pixels = 0;
      int darkest_channel = 255;
      const int radius = static_cast<int>(std::ceil(3.0 * factor));
      for (int offset = -radius; offset <= radius; ++offset) {
        const QPoint sample = edge.toPoint() + QPoint(0, offset);
        ASSERT_TRUE(output.rect().contains(sample));
        const QColor pixel = output.pixelColor(sample);
        if (pixel.red() >= 245 && pixel.green() >= 245 && pixel.blue() >= 245) {
          ++white_pixels;
        }
        darkest_channel = std::min(darkest_channel, pixel.red());
      }
      if (background == QColor(Qt::black)) {
        EXPECT_GE(white_pixels, std::max(1, static_cast<int>(2.0f * factor) - 1));
      } else {
        EXPECT_LT(darkest_channel, 220);
        const QColor center = output.pixelColor(edge.toPoint());
        EXPECT_GE(center.red(), 245);
        EXPECT_GE(center.green(), 245);
        EXPECT_GE(center.blue(), 245);
      }
    }
  }
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
