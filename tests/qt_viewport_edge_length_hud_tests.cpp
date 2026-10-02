////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "ui/qt_viewport_edge_length_hud.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "support/qt_viewport_input_test_helpers.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <QApplication>
#include <QAbstractNativeEventFilter>
#include <QBackingStore>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QEnterEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QFrame>
#include <QFontInfo>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPalette>
#include <QPoint>
#include <QPixmap>
#include <QRegion>
#include <QString>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace {

/// Records expensive retained-widget changes without requiring a GPU viewport.
class EdgeHudWidgetEventCounter final : public QObject {
public:
  /// Counts geometry, visibility, and stacking changes for the retained label.
  struct Counts {
    int move = 0;
    int resize = 0;
    int show = 0;
    int hide = 0;
    int z_order = 0;
    int paint = 0;

    friend bool operator==(const Counts &, const Counts &) = default;
  };

  [[nodiscard]] Counts counts() const { return counts_; }

protected:
  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched != nullptr && event != nullptr) {
      switch (event->type()) {
      case QEvent::Move:
        ++counts_.move;
        break;
      case QEvent::Resize:
        ++counts_.resize;
        break;
      case QEvent::Show:
      case QEvent::ShowToParent:
        ++counts_.show;
        break;
      case QEvent::Hide:
      case QEvent::HideToParent:
        ++counts_.hide;
        break;
      case QEvent::ZOrderChange:
        ++counts_.z_order;
        break;
      case QEvent::Paint:
        ++counts_.paint;
        break;
      default:
        break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  Counts counts_;
};

/// Keeps ordinary test ancestors alien without changing later fixtures' policy.
class ScopedExplicitHudNativeWidgets final {
public:
  ScopedExplicitHudNativeWidgets()
      : previous_(QCoreApplication::testAttribute(
            Qt::AA_DontCreateNativeWidgetSiblings)) {
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  }

  ~ScopedExplicitHudNativeWidgets() {
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings,
                                   previous_);
  }

private:
  bool previous_;
};

quader::ViewportOverlayFrame hover_edge_frame(quader::QVec3 start,
                                               quader::QVec3 end) {
  quader::ViewportOverlayFrame frame;
  frame.lines.push_back({
      .layer = quader::ViewportOverlayLayer::HoverEdge,
      .start = start,
      .end = end,
      .source = quader::ViewportOverlaySource::ComponentHover,
  });
  return frame;
}

quader::ViewportOverlayFrame hover_edge_frame_with_inside_reference(
    quader::QVec3 start, quader::QVec3 end, quader::QVec3 inside_reference) {
  quader::ViewportOverlayFrame frame = hover_edge_frame(start, end);
  frame.lines.front().element = {
      .object_id = 17,
      .edge_a = 3,
      .edge_b = 9,
      .face_id = 23,
  };
  frame.hover_edge_dimension_context =
      quader::ViewportHoverEdgeDimensionContext{
          .inside_reference = inside_reference,
          .element = frame.lines.front().element,
      };
  return frame;
}

#if defined(_WIN32)
/// Observes the old native rectangle after HUD filters retire its glyph pixels,
/// but before Windows applies the ancestor's proposed geometry.
class EdgeHudNativeGeometryObserver final : public QAbstractNativeEventFilter {
public:
  EdgeHudNativeGeometryObserver() {
    QCoreApplication::instance()->installNativeEventFilter(this);
  }

  ~EdgeHudNativeGeometryObserver() override {
    QCoreApplication::instance()->removeNativeEventFilter(this);
  }

  void set_observer(std::function<void(const MSG &)> observer) {
    observer_ = std::move(observer);
  }

  bool nativeEventFilter(const QByteArray &event_type, void *message,
                         qintptr *result) override {
    (void)result;
    if (observer_ && message != nullptr &&
        event_type == QByteArrayLiteral("windows_generic_MSG")) {
      observer_(*static_cast<const MSG *>(message));
    }
    return false;
  }

private:
  std::function<void(const MSG &)> observer_;
};

QImage *native_backing_image(QWidget &window) {
  QBackingStore *store = window.backingStore();
  return store != nullptr ? dynamic_cast<QImage *>(store->paintDevice())
                          : nullptr;
}

QRect native_label_pixel_bounds(const QLabel &label, QWidget &window) {
  const QPoint origin = label.mapTo(&window, QPoint{});
  const qreal ratio = window.devicePixelRatioF();
  const int left = static_cast<int>(std::floor(origin.x() * ratio));
  const int top = static_cast<int>(std::floor(origin.y() * ratio));
  const int right = static_cast<int>(
      std::ceil((origin.x() + label.width()) * ratio));
  const int bottom = static_cast<int>(
      std::ceil((origin.y() + label.height()) * ratio));
  return {left, top, right - left, bottom - top};
}

int native_dimension_pixel_count(const QImage &image, const QRect &bounds,
                                const std::array<QColor, 3> &colors) {
  int count = 0;
  const QRect clipped = bounds.intersected(image.rect());
  for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
    for (int x = clipped.left(); x <= clipped.right(); ++x) {
      const QColor pixel = image.pixelColor(x, y);
      // The authored axis alpha is flattened by an opaque Windows backing
      // store. Its RGB distance from the authored color is bounded by the
      // missing alpha, unlike the charcoal background and black outline.
      const bool dimension_pixel = pixel.alpha() > 0 && std::any_of(
          colors.begin(), colors.end(), [&](const QColor &color) {
            const int tolerance = 256 - color.alpha();
            return std::abs(pixel.red() - color.red()) <= tolerance &&
                   std::abs(pixel.green() - color.green()) <= tolerance &&
                   std::abs(pixel.blue() - color.blue()) <= tolerance;
          });
      count += dimension_pixel ? 1 : 0;
    }
  }
  return count;
}
#endif

/// Hosts a real retained label while camera and overlay inputs remain headless.
class QtViewportEdgeLengthHudTests : public ::testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
    parent_ = std::make_unique<QWidget>();
    parent_->resize(400, 300);
    parent_->setAttribute(Qt::WA_DontShowOnScreen, true);
    parent_->show();
    hud_ = new quader::QtViewportEdgeLengthHud(
        *parent_, quader::ViewportPaneId::Perspective);
    label_ = parent_->findChild<QLabel *>(
        QStringLiteral("viewportEdgeLength_perspective"));
    ASSERT_NE(label_, nullptr);
    footprint_labels_[0] = parent_->findChild<QLabel *>(
        QStringLiteral("viewportFootprintDimension0_perspective"));
    footprint_labels_[1] = parent_->findChild<QLabel *>(
        QStringLiteral("viewportFootprintDimension1_perspective"));
    ASSERT_NE(footprint_labels_[0], nullptr);
    ASSERT_NE(footprint_labels_[1], nullptr);
    const std::array selection_names{
        QStringLiteral("viewportSelectionDimensionX_perspective"),
        QStringLiteral("viewportSelectionDimensionY_perspective"),
        QStringLiteral("viewportSelectionDimensionZ_perspective")};
    for (std::size_t axis = 0U; axis < selection_labels_.size(); ++axis) {
      selection_labels_[axis] = parent_->findChild<QLabel *>(selection_names[axis]);
      ASSERT_NE(selection_labels_[axis], nullptr);
    }
    camera_.focus_origin(64.0f);
    camera_.view_axis(quader::ViewportViewAxis::Z, 1);
    camera_.set_viewport_size(800.0f, 600.0f);
  }

  void present(const quader::ViewportOverlayFrame &frame, bool enabled = true,
               const quader::ModelingBoxPreview *footprint_preview = nullptr,
               bool footprint_enabled = false,
               double font_size_points = 12.0) {
    hud_->present(
        frame, quader::RenderSignatureBuilder::overlay_frame_signature(frame),
        camera_, parent_->rect(), enabled, font_size_points, footprint_preview,
        footprint_enabled);
  }

  void present_footprint(const quader::ModelingBoxPreview &preview,
                         bool enabled = true) {
    present({}, false, &preview, enabled);
  }

  void present_selection(const quader::ViewportSelectionDimensionBounds &bounds,
                         std::uint64_t signature = 1U, bool enabled = true) {
    hud_->present({}, 1U, camera_, parent_->rect(), false, 12.0, nullptr, false,
                   &bounds, signature, enabled);
  }

  void expect_selection_anchor(std::size_t axis, quader::QVec3 midpoint) const {
    ASSERT_FALSE(selection_labels_[axis]->isHidden());
    const auto projected = camera_.project_world_point(midpoint);
    ASSERT_TRUE(projected.visible);
    const QPoint expected{
        qRound(projected.position.x * parent_->width() / camera_.viewport_size().x),
        qRound(projected.position.y * parent_->height() / camera_.viewport_size().y)};
    EXPECT_LE((selection_labels_[axis]->geometry().center() - expected).manhattanLength(),
              2);
    EXPECT_TRUE(parent_->rect().contains(selection_labels_[axis]->geometry()));
  }

  quader::QVec3 point_at_screen(quader::QVec2 screen, float depth = 64.0f) const {
    const quader::ViewportCameraPose pose = camera_.pose();
    const quader::QVec3 forward = quader::normalize(pose.target - pose.eye);
    const quader::ViewportRay ray = camera_.ray_from_viewport_point(screen);
    return ray.origin +
           ray.direction *
               ((depth - quader::dot(ray.origin - pose.eye, forward)) /
                quader::dot(ray.direction, forward));
  }

  void expect_label_outside_at(quader::QVec2 render_anchor,
                               const QRect &logical_bounds) const {
    ASSERT_FALSE(label_->isHidden());
    EXPECT_TRUE(logical_bounds.contains(label_->geometry()));
    const quader::QVec2 render_size = camera_.viewport_size();
    const QPointF logical_anchor{
        logical_bounds.x() +
            render_anchor.x * logical_bounds.width() / render_size.x,
        logical_bounds.y() +
            render_anchor.y * logical_bounds.height() / render_size.y,
    };
    const QPointF offset = label_->geometry().center() - logical_anchor;
    const qreal minimum_distance =
        static_cast<qreal>(label_->height()) * 0.5 + 3.0;
    EXPECT_GE(std::hypot(offset.x(), offset.y()), minimum_distance - 1.5);
  }

  void expect_label_away_from_reference(
      const QLabel &label, quader::QVec3 edge_start, quader::QVec3 edge_end,
      quader::QVec3 inside_reference) const {
    const quader::ViewportScreenPoint projected_start =
        camera_.project_world_point(edge_start);
    const quader::ViewportScreenPoint projected_end =
        camera_.project_world_point(edge_end);
    const quader::ViewportScreenPoint projected_reference =
        camera_.project_world_point(inside_reference);
    ASSERT_TRUE(projected_start.visible);
    ASSERT_TRUE(projected_end.visible);
    ASSERT_TRUE(projected_reference.visible);
    const auto logical_point = [&](quader::QVec2 render_point) {
      return QPointF{
          parent_->rect().x() +
              render_point.x * parent_->rect().width() /
                  camera_.viewport_size().x,
          parent_->rect().y() +
              render_point.y * parent_->rect().height() /
                  camera_.viewport_size().y,
      };
    };
    const QPointF start = logical_point(projected_start.position);
    const QPointF end = logical_point(projected_end.position);
    const QPointF reference = logical_point(projected_reference.position);
    const QPointF midpoint{(start.x() + end.x()) * 0.5,
                           (start.y() + end.y()) * 0.5};
    const QPointF label_offset = label.geometry().center() - midpoint;
    const QPointF outside = midpoint - reference;
    EXPECT_GT(label_offset.x() * outside.x() +
                  label_offset.y() * outside.y(),
              0.0);
  }

  int argc_ = 1;
  char application_name_[33] = "quader-edge-length-hud-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<QWidget> parent_;
  quader::QtViewportEdgeLengthHud *hud_ = nullptr;
  QLabel *label_ = nullptr;
  std::array<QLabel *, 2> footprint_labels_ = {};
  std::array<QLabel *, 3> selection_labels_ = {};
  quader::ViewportCameraController camera_;
};

TEST_F(QtViewportEdgeLengthHudTests,
       TransformHandleHoverHidesUnderlyingEdgeDimensionUntilPointerLeaves) {
  quader::SceneSession session;
  quader::QuaderEditorApi editor(
      quader::QuaderApiSessionInternalAccess::from_scene_session(session));
  ASSERT_TRUE(editor.commands().set_keybinding_storage_path({}).ok());
  editor.preferences().set_runtime_hooks({.gizmos_visible = [] { return true; }});
  quader_poly::Document topology;
  const std::array vertices{
      quader_poly::add_vertex(topology, {-2, 0, 0}),
      quader_poly::add_vertex(topology, {-2, -2, 0}),
      quader_poly::add_vertex(topology, {2, -2, 0}),
      quader_poly::add_vertex(topology, {2, 0, 0})};
  ASSERT_NE(quader_poly::add_face(topology, vertices), quader_poly::kInvalidElementId);
  const auto mesh = editor.modeling().create_mesh(
      quader::QuaderPolygonDocumentInternalAccess::from_backend(std::move(topology)));
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(editor.selection().set_mode(quader::QuaderSelectionMode::Edge).ok());
  ASSERT_TRUE(editor.selection().select_edge(mesh.value(), vertices[0], vertices[3]).ok());
  ASSERT_TRUE(editor.main_viewport().set_view(quader::ViewportView::Back).ok());
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  session.history().clear();

  parent_->resize(800, 600);
  quader::QtViewportPanel panel(parent_.get());
  panel.setGeometry(parent_->rect());
  panel.attach_workspace(session.viewport_workspace());
  panel.set_startup_presentation_blocked(false);
  panel.show();
  QCoreApplication::processEvents();
  auto *surface = panel.surface(quader::ViewportPaneId::Perspective);
  ASSERT_NE(surface, nullptr);
  // Native-child exposure is intentionally disabled in this headless host.
  // Give its visible host the client extent normally applied by the renderer.
  quader::test_support::resize_viewport_surface(panel, *surface, {800, 600});
  auto *label = panel.findChild<QLabel *>(QStringLiteral("viewportEdgeLength_perspective"));
  ASSERT_NE(label, nullptr);
  auto &camera = session.viewport_workspace().camera(quader::ViewportPaneId::Perspective);
  camera.focus_origin(8);
  QEnterEvent enter({400, 300}, {400, 300}, {500, 400});
  QFocusEvent focus(QEvent::FocusIn);
  quader::test_support::send_visible_viewport_event(surface, &enter);
  QApplication::sendEvent(surface, &focus);
  quader::SceneRenderBridge bridge;
  quader::RenderOverlaySnapshotCache cache;
  const auto frame = [&] {
    panel.process_input(editor, session.scene(), session.assets(), session.modeling(), {},
                        session.native_tools(), &session.point_placement(), 1.0 / 60.0);
    panel.prepare_viewports(editor, bridge, cache, session.scene(), session.assets(),
                            session.modeling(), session.native_tools());
  };
  frame();
  const auto hover = [&](quader::QVec3 world) {
    const auto projected = camera.project_world_point(world);
    EXPECT_TRUE(projected.visible);
    const QPointF local{projected.position.x / surface->devicePixelRatio(),
                         projected.position.y / surface->devicePixelRatio()};
    QMouseEvent event(QEvent::MouseMove, local,
                      QPointF(surface->mapToGlobal(local.toPoint())),
                      Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    quader::test_support::send_visible_viewport_event(surface, &event);
    frame();
  };
  // The selected edge spans the Move pivot; its positive half is beneath X.
  hover({-1, 0, 0});
  ASSERT_EQ(session.modeling().selection_hover_summary().target.kind,
            quader::ModelingActiveSelectionKind::Edge);
  ASSERT_FALSE(label->isHidden());
  EXPECT_EQ(label->text(), QStringLiteral("128.00"));
  hover({0.6f, 0, 0});
  EXPECT_TRUE(label->isHidden());
  for (int idle = 0; idle < 8; ++idle) frame();
  EXPECT_TRUE(label->isHidden());

  // Verify an eligible edge is actually under the same screen position.
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Select).ok());
  frame();
  ASSERT_FALSE(label->isHidden());
  EXPECT_EQ(label->text(), QStringLiteral("128.00"));
  ASSERT_TRUE(editor.tools().activate(quader::ViewportToolId::Move).ok());
  frame();
  EXPECT_TRUE(label->isHidden());
  hover({-1, 0, 0});
  EXPECT_FALSE(label->isHidden());
  EXPECT_EQ(label->text(), QStringLiteral("128.00"));
  EXPECT_FALSE(session.modeling().transform_drag_active());
  EXPECT_EQ(session.history().undo_count(), 0u);
}

TEST_F(QtViewportEdgeLengthHudTests,
       UsesFullWorldSpaceLengthInMapUnitsWithOutlinedShapedLabels) {
  // The 3D length is 13; its screen-plane length is only 5. World endpoints
  // already carry object, instance, and hierarchy transforms from projection.
  present(hover_edge_frame({-1.5f, -2.0f, -6.0f}, {1.5f, 2.0f, 6.0f}));

  ASSERT_FALSE(label_->isHidden());
  EXPECT_EQ(label_->text(),
            QString::number(
                13.0 * quader::viewport_host_defaults().map_units_per_world_unit,
                'f', 2));
  EXPECT_FALSE(label_->autoFillBackground());
  EXPECT_EQ(label_->frameStyle(), QFrame::NoFrame);
  EXPECT_EQ(label_->margin(), 0);
  EXPECT_FALSE(label_->testAttribute(Qt::WA_TranslucentBackground));
  EXPECT_TRUE(label_->testAttribute(Qt::WA_NoSystemBackground));
  EXPECT_EQ(label_->foregroundRole(), QPalette::WindowText);
  EXPECT_FALSE(label_->mask().isEmpty());
  EXPECT_FALSE(label_->mask() == QRegion(label_->rect()));
  const qreal raster_dpr =
      label_->property("quaderDimensionRasterDevicePixelRatio").toReal();
  EXPECT_DOUBLE_EQ(raster_dpr, label_->devicePixelRatioF());
  EXPECT_EQ(label_->property("quaderDimensionRasterPixelSize").toSize(),
            QSize(std::max(1, static_cast<int>(
                                  std::ceil(label_->width() * raster_dpr))),
                  std::max(1, static_cast<int>(
                                  std::ceil(label_->height() * raster_dpr)))));
  const QFont application_font = QApplication::font();
  EXPECT_EQ(QFontInfo(label_->font()).family(),
            QFontInfo(application_font).family());
  EXPECT_DOUBLE_EQ(label_->font().pointSizeF(), 12.0);
  EXPECT_EQ(label_->font().styleStrategy(), application_font.styleStrategy());
  EXPECT_EQ(label_->font().hintingPreference(),
            application_font.hintingPreference());
  EXPECT_TRUE(label_->testAttribute(Qt::WA_NativeWindow));
  EXPECT_TRUE(label_->testAttribute(Qt::WA_TransparentForMouseEvents));
  EXPECT_EQ(label_->focusPolicy(), Qt::NoFocus);

  QImage painted(label_->size(), QImage::Format_ARGB32_Premultiplied);
  painted.fill(Qt::transparent);
  label_->render(&painted);
  int opaque_black_pixels = 0;
  int opaque_white_pixels = 0;
  int blended_or_colored_pixels = 0;
  QColor first_blended_or_colored_pixel;
  bool found_transparent = false;
  for (int y = 0; y < painted.height(); ++y) {
    for (int x = 0; x < painted.width(); ++x) {
      const QColor pixel = painted.pixelColor(x, y);
      found_transparent = found_transparent || pixel.alpha() == 0;
      if (pixel.alpha() == 255 && pixel.red() == 0 && pixel.green() == 0 &&
          pixel.blue() == 0) {
        ++opaque_black_pixels;
      }
      if (pixel.alpha() == 255 && pixel.red() >= 250 &&
          pixel.green() >= 250 && pixel.blue() >= 250) {
        ++opaque_white_pixels;
      }
      if (pixel.alpha() != 0) {
        const bool exact_black =
            pixel.alpha() == 255 && pixel.red() == 0 && pixel.green() == 0 &&
            pixel.blue() == 0;
        const bool exact_white = pixel.alpha() == 255 && pixel.red() == 255 &&
                                 pixel.green() == 255 && pixel.blue() == 255;
        if (!exact_black && !exact_white) {
          if (blended_or_colored_pixels == 0) {
            first_blended_or_colored_pixel = pixel;
          }
          ++blended_or_colored_pixels;
        }
      }
    }
  }
  EXPECT_GT(opaque_black_pixels, 20)
      << "The outline must contain a substantial pitch-black opaque border.";
  EXPECT_GT(opaque_white_pixels, 8)
      << "The glyph must contain a substantial bright opaque core.";
  EXPECT_EQ(blended_or_colored_pixels, 0)
      << "Dimension labels must contain only exact opaque black or white; "
         "gray antialiasing fringes make them blurry over the viewport. "
      << "First unexpected RGBA pixel: ("
      << first_blended_or_colored_pixel.red() << ", "
      << first_blended_or_colored_pixel.green() << ", "
      << first_blended_or_colored_pixel.blue() << ", "
      << first_blended_or_colored_pixel.alpha() << ").";
  EXPECT_TRUE(found_transparent);

  for (const qreal scale : {1.25, 1.5, 2.0}) {
    QImage scaled(
        QSize{static_cast<int>(std::ceil(label_->width() * scale)),
              static_cast<int>(std::ceil(label_->height() * scale))},
        QImage::Format_ARGB32_Premultiplied);
    scaled.fill(Qt::transparent);
    {
      QPainter painter(&scaled);
      painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
      painter.scale(scale, scale);
      label_->render(&painter);
    }
    int scaled_non_binary_pixels = 0;
    for (int y = 0; y < scaled.height(); ++y) {
      for (int x = 0; x < scaled.width(); ++x) {
        const QColor pixel = scaled.pixelColor(x, y);
        const bool transparent = pixel.alpha() == 0;
        const bool exact_black =
            pixel.alpha() == 255 && pixel.red() == 0 && pixel.green() == 0 &&
            pixel.blue() == 0;
        const bool exact_white = pixel.alpha() == 255 && pixel.red() == 255 &&
                                 pixel.green() == 255 && pixel.blue() == 255;
        scaled_non_binary_pixels +=
            !transparent && !exact_black && !exact_white ? 1 : 0;
      }
    }
    EXPECT_EQ(scaled_non_binary_pixels, 0)
        << "Nearest-neighbor dimension output must stay binary at a "
        << scale << "x device scale.";
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       AppliesConfiguredFontSizeOnlyWhenTheEffectiveValueChanges) {
  const quader::ViewportOverlayFrame frame =
      hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  present(frame, true, nullptr, false, 6.0);
  ASSERT_FALSE(label_->isHidden());
  EXPECT_DOUBLE_EQ(label_->font().pointSizeF(), 6.0);
  const QSize small_size = label_->size();

  present(frame, true, nullptr, false, 14.0);
  EXPECT_DOUBLE_EQ(label_->font().pointSizeF(), 14.0);
  EXPECT_GT(label_->width(), small_size.width());
  EXPECT_GT(label_->height(), small_size.height());

  EdgeHudWidgetEventCounter events;
  label_->installEventFilter(&events);
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts configured = events.counts();
  for (int repetition = 0; repetition < 64; ++repetition) {
    present(frame, true, nullptr, false, 14.0);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), configured);
}

TEST_F(QtViewportEdgeLengthHudTests,
       AppliesInterfaceScaleAfterTheAuthoredDimensionFontPreference) {
  const quader::ViewportOverlayFrame frame =
      hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  present(frame, true, nullptr, false, 12.0);
  ASSERT_FALSE(label_->isHidden());
  const QSize baseline_size = label_->size();

  hud_->apply_ui_scale(quader::QtUiScaleMetrics{2.0F, 1U});
  present(frame, true, nullptr, false, 12.0);
  EXPECT_DOUBLE_EQ(label_->font().pointSizeF(), 24.0);
  EXPECT_GT(label_->width(), baseline_size.width());
  EXPECT_GT(label_->height(), baseline_size.height());

  hud_->apply_ui_scale(quader::QtUiScaleMetrics{1.0F, 2U});
  present(frame, true, nullptr, false, 12.0);
  EXPECT_DOUBLE_EQ(label_->font().pointSizeF(), 12.0);
  EXPECT_EQ(label_->size(), baseline_size);
}

TEST_F(QtViewportEdgeLengthHudTests, FormatsEveryMeasurementWithTwoDecimals) {
  const float world_per_map_unit = static_cast<float>(
      1.0 / quader::viewport_host_defaults().map_units_per_world_unit);
  present(hover_edge_frame({}, {64.0f * world_per_map_unit, 0.0f, 0.0f}));
  EXPECT_EQ(label_->text(), QStringLiteral("64.00"));

  present(hover_edge_frame({}, {world_per_map_unit, world_per_map_unit, 0.0f}));
  EXPECT_EQ(label_->text(), QStringLiteral("1.41"));

  present(hover_edge_frame({}, {0.0625f * world_per_map_unit, 0.0f, 0.0f}));
  EXPECT_EQ(label_->text(), QStringLiteral("0.06"));
}

TEST_F(QtViewportEdgeLengthHudTests,
       NativeHitTestPassesThroughAndSiblingViewportStillReceivesClicks) {
#if defined(_WIN32)
  if (QGuiApplication::platformName() != QStringLiteral("windows")) {
    GTEST_SKIP() << "Native HWND hit testing requires the Windows Qt platform.";
  }
  auto *surface =
      new quader::QtViewportSurface(quader::ViewportPaneId::Perspective);
  QWidget *container = QWidget::createWindowContainer(surface, parent_.get());
  container->setGeometry(parent_->rect());
  container->show();
  present(hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}));
  QCoreApplication::processEvents();
  ASSERT_FALSE(label_->isHidden());

  const HWND label_window = reinterpret_cast<HWND>(label_->winId());
  ASSERT_TRUE(IsWindow(label_window));
  EXPECT_EQ(GetWindowLongPtrW(label_window, GWL_EXSTYLE) & WS_EX_LAYERED, 0);
  RECT native_rect{};
  ASSERT_TRUE(GetClientRect(label_window, &native_rect));
  POINT native_center{(native_rect.left + native_rect.right) / 2,
                       (native_rect.top + native_rect.bottom) / 2};
  ASSERT_TRUE(ClientToScreen(label_window, &native_center));
  const LPARAM hit_position = MAKELPARAM(native_center.x, native_center.y);
  // Exercise the actual QWidget HWND procedure, not only the Qt attribute.
  // Returning HTTRANSPARENT tells Windows to continue to underlying windows
  // in this UI thread; no cursor movement or desktop input injection is needed.
  EXPECT_EQ(SendMessageW(label_window, WM_NCHITTEST, 0, hit_position),
            static_cast<LRESULT>(HTTRANSPARENT));

  const QPoint global = label_->mapToGlobal(label_->rect().center());
  const QPoint local = surface->mapFromGlobal(global);
  QEnterEvent enter{QPointF(local), QPointF(local), QPointF(global)};
  QApplication::sendEvent(surface, &enter);
  for (const Qt::MouseButton button : {Qt::LeftButton, Qt::MiddleButton}) {
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                      button, button, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local),
                        QPointF(global), button, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(surface, &press);
    QApplication::sendEvent(surface, &release);
  }
  const quader::QtViewportInputFrame input =
      surface->take_input_frame(0.0f, false);
  EXPECT_TRUE(input.left_pressed);
  EXPECT_TRUE(input.left_released);
  EXPECT_TRUE(input.input.middle_pressed);
  EXPECT_TRUE(input.input.middle_released);
  EXPECT_FALSE(label_->hasFocus());
#else
  GTEST_SKIP() << "Native HWND hit testing is Windows-only.";
#endif
}

TEST_F(QtViewportEdgeLengthHudTests,
       PlacesLabelOutsideTheProjectedEdgeAndUpdatesOnResize) {
  const quader::ViewportOverlayFrame frame =
      hover_edge_frame({1.0f, 3.0f, 0.0f}, {3.0f, 3.0f, 0.0f});
  const std::uint64_t signature =
      quader::RenderSignatureBuilder::overlay_frame_signature(frame);
  const quader::ViewportScreenPoint projected =
      camera_.project_world_point({2.0f, 3.0f, 0.0f});
  ASSERT_TRUE(projected.visible);
  const QRect logical_bounds{10, 20, 320, 240};
  hud_->present(frame, signature, camera_, logical_bounds, true, 7.0);

  ASSERT_FALSE(label_->isHidden());
  EXPECT_NEAR(label_->geometry().center().x(),
              logical_bounds.x() + projected.position.x * 320.0f / 800.0f,
              1.0f);
  EXPECT_LT(label_->geometry().center().y(),
            logical_bounds.y() + projected.position.y * 240.0f / 600.0f);

  const QRect resized_bounds{0, 0, 400, 300};
  hud_->present(frame, signature, camera_, resized_bounds, true, 7.0);
  EXPECT_NEAR(label_->geometry().center().x(), projected.position.x * 0.5f,
              1.0f);
  EXPECT_LT(label_->geometry().center().y(), projected.position.y * 0.5f);

  camera_.unlock_orthographic_view();
  hud_->present(frame, signature, camera_, resized_bounds, true, 7.0);
  const quader::ViewportScreenPoint perspective =
      camera_.project_world_point({2.0f, 3.0f, 0.0f});
  ASSERT_FALSE(label_->isHidden());
  EXPECT_NEAR(label_->geometry().center().x(), perspective.position.x * 0.5f,
              1.0f);
  EXPECT_LT(label_->geometry().center().y(), perspective.position.y * 0.5f);
}

TEST_F(QtViewportEdgeLengthHudTests,
       KeepsTheWholeLabelOutsideTheHoveredMeshSide) {
  const quader::QVec3 start = point_at_screen({400.0f, 180.0f});
  const quader::QVec3 end = point_at_screen({400.0f, 420.0f});
  const quader::QVec3 inside = point_at_screen({300.0f, 300.0f});
  present(hover_edge_frame_with_inside_reference(start, end, inside));

  ASSERT_FALSE(label_->isHidden());
  const qreal logical_edge_x = parent_->rect().width() * 0.5;
  EXPECT_GE(label_->geometry().left(), logical_edge_x + 2.0)
      << "Interior is left of the vertical edge, so every label pixel must be "
         "on its right/outside side.";
  expect_label_away_from_reference(*label_, start, end, inside);
}

TEST_F(QtViewportEdgeLengthHudTests,
       NeverFlipsAReferencedDimensionBackInsideAtThePaneBorder) {
  const quader::QVec3 start = point_at_screen({790.0f, 180.0f});
  const quader::QVec3 end = point_at_screen({790.0f, 420.0f});
  const quader::QVec3 inside = point_at_screen({700.0f, 300.0f});
  present(hover_edge_frame_with_inside_reference(start, end, inside));

  EXPECT_TRUE(label_->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       FootprintDimensionsProgressFromNothingToLineToRectangle) {
  quader::ModelingBoxPreview preview{
      .ok = true,
      .drag_state = quader::ModelingBoxDragState::Footprint,
  };
  present_footprint(preview);
  EXPECT_TRUE(footprint_labels_[0]->isHidden());
  EXPECT_TRUE(footprint_labels_[1]->isHidden());

  preview.footprint = {
      quader::QVec3{-2.0f, 0.0f, 0.0f},
      quader::QVec3{2.0f, 0.0f, 0.0f},
  };
  preview.footprint_count = 2U;
  present_footprint(preview);
  ASSERT_FALSE(footprint_labels_[0]->isHidden());
  EXPECT_TRUE(footprint_labels_[1]->isHidden());
  EXPECT_FALSE(footprint_labels_[0]->mask().isEmpty());
  EXPECT_EQ(
      footprint_labels_[0]->text(),
      QString::number(
          4.0 * quader::viewport_host_defaults().map_units_per_world_unit,
          'f', 2));
  EXPECT_LT(footprint_labels_[0]->geometry().center().y(),
            parent_->rect().center().y());

  preview.valid = true;
  preview.footprint = {
      quader::QVec3{-2.0f, -1.0f, 0.0f},
      quader::QVec3{-2.0f, 1.0f, 0.0f},
      quader::QVec3{2.0f, 1.0f, 0.0f},
      quader::QVec3{2.0f, -1.0f, 0.0f},
  };
  preview.footprint_count = 4U;
  present_footprint(preview);
  ASSERT_FALSE(footprint_labels_[0]->isHidden());
  ASSERT_FALSE(footprint_labels_[1]->isHidden());
  EXPECT_FALSE(footprint_labels_[0]->mask().isEmpty());
  EXPECT_FALSE(footprint_labels_[1]->mask().isEmpty());
  EXPECT_EQ(
      footprint_labels_[0]->text(),
      QString::number(
          4.0 * quader::viewport_host_defaults().map_units_per_world_unit,
          'f', 2));
  EXPECT_EQ(
      footprint_labels_[1]->text(),
      QString::number(
          2.0 * quader::viewport_host_defaults().map_units_per_world_unit,
          'f', 2));
  expect_label_away_from_reference(
      *footprint_labels_[0], preview.footprint[1], preview.footprint[2], {});
  expect_label_away_from_reference(
      *footprint_labels_[1], preview.footprint[3], preview.footprint[2], {});

  preview.valid = false;
  preview.footprint_count = 0U;
  present_footprint(preview);
  EXPECT_TRUE(footprint_labels_[0]->isHidden());
  EXPECT_TRUE(footprint_labels_[1]->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       HidesWhenDisabledOrNoTrueComponentHoverEdgeExists) {
  const quader::ViewportOverlayFrame edge =
      hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  present(edge);
  ASSERT_FALSE(label_->isHidden());
  present(edge, false);
  EXPECT_TRUE(label_->isHidden());
  present(edge);
  ASSERT_FALSE(label_->isHidden());
  present({});
  EXPECT_TRUE(label_->isHidden());

  quader::ViewportOverlayFrame unrelated = edge;
  unrelated.lines.front().source = quader::ViewportOverlaySource::Hover;
  present(unrelated);
  EXPECT_TRUE(label_->isHidden());
  unrelated = edge;
  unrelated.lines.front().layer = quader::ViewportOverlayLayer::HoverFaceEdge;
  present(unrelated);
  EXPECT_TRUE(label_->isHidden());
  unrelated = edge;
  unrelated.lines.front().layer = quader::ViewportOverlayLayer::SelectedEdge;
  unrelated.lines.front().source =
      quader::ViewportOverlaySource::ComponentSelection;
  present(unrelated);
  EXPECT_TRUE(label_->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       HoverToggleDoesNotSuppressActiveFootprintDimensions) {
  quader::ModelingBoxPreview preview{
      .ok = true,
      .drag_state = quader::ModelingBoxDragState::Footprint,
      .footprint = {
          quader::QVec3{-2.0f, 0.0f, 0.0f},
          quader::QVec3{2.0f, 0.0f, 0.0f},
      },
      .footprint_count = 2U,
  };
  present(hover_edge_frame({-1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}),
          false, &preview, true);

  EXPECT_TRUE(label_->isHidden());
  EXPECT_FALSE(footprint_labels_[0]->isHidden());
  EXPECT_TRUE(footprint_labels_[1]->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       KeepsFullLengthOnVisibleSegmentWhenWorldMidpointIsOffscreen) {
  const QRect logical_bounds{17, 29, 351, 197};
  for (const bool perspective : {false, true}) {
    SCOPED_TRACE(perspective ? "perspective" : "orthographic");
    if (perspective) {
      camera_.unlock_orthographic_view();
    }
    const quader::QVec3 start = point_at_screen({200.0f, 300.0f});
    const quader::QVec3 end = point_at_screen({2200.0f, 300.0f});
    const quader::ViewportOverlayFrame frame = hover_edge_frame(start, end);
    ASSERT_GT(camera_.project_world_point((start + end) * 0.5f).position.x,
              camera_.viewport_size().x);

    hud_->present(frame,
                  quader::RenderSignatureBuilder::overlay_frame_signature(frame),
                  camera_, logical_bounds, true, 7.0);

    // The visible interval is [200, 800], irrespective of the edge's full length.
    expect_label_outside_at({500.0f, 300.0f}, logical_bounds);
    EXPECT_NEAR(label_->text().toDouble(),
                quader::length(end - start) *
                    quader::viewport_host_defaults().map_units_per_world_unit,
                0.1);
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       KeepsLabelOnCrossingSegmentWithBothEndpointsOffscreen) {
  for (const bool perspective : {false, true}) {
    SCOPED_TRACE(perspective ? "perspective" : "orthographic");
    if (perspective) {
      camera_.unlock_orthographic_view();
    }
    // Both endpoints and the world midpoint are outside the image. The visible
    // diagonal runs from (0, 100) to (800, 500), through its center.
    present(hover_edge_frame(point_at_screen({-400.0f, -100.0f}),
                            point_at_screen({2800.0f, 1500.0f})));
    expect_label_outside_at({400.0f, 300.0f}, parent_->rect());
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       ClampsFullLabelInsidePaneForVisibleBorderSliver) {
  const QRect logical_bounds{17, 29, 351, 197};
  const quader::ViewportOverlayFrame frame =
      hover_edge_frame(point_at_screen({-400.0f, 4.0f}),
                       point_at_screen({8.0f, 4.0f}));
  hud_->present(frame,
                quader::RenderSignatureBuilder::overlay_frame_signature(frame),
                camera_, logical_bounds, true, 7.0);

  ASSERT_FALSE(label_->isHidden());
  EXPECT_TRUE(logical_bounds.contains(label_->geometry()));
  EXPECT_LT(label_->geometry().center().x(),
            logical_bounds.left() + label_->width());
  EXPECT_LT(label_->geometry().center().y(),
            logical_bounds.top() + label_->height());
}

TEST_F(QtViewportEdgeLengthHudTests,
       ClipsPerspectiveDepthBeforeChoosingVisibleScreenMidpoint) {
  camera_.unlock_orthographic_view();
  camera_.set_clip_planes(1.0f, 100.0f);

  const quader::QVec3 front = point_at_screen({200.0f, 300.0f}, 10.0f);
  const quader::QVec3 behind = point_at_screen({600.0f, 300.0f}, -30.0f);
  ASSERT_FALSE(camera_.project_world_point((front + behind) * 0.5f).visible);
  present(hover_edge_frame(front, behind));
  // This edge reaches the left screen boundary before entering the near plane.
  expect_label_outside_at({100.0f, 300.0f}, parent_->rect());

  const quader::QVec3 near_end = point_at_screen({200.0f, 300.0f}, 25.0f);
  const quader::QVec3 far_end = point_at_screen({600.0f, 300.0f}, 225.0f);
  ASSERT_FALSE(camera_.project_world_point((near_end + far_end) * 0.5f).visible);
  present(hover_edge_frame(near_end, far_end));
  // At depth 100 the far-clipped endpoint projects to x=537.5, not x=600.
  expect_label_outside_at({368.75f, 300.0f}, parent_->rect());
}

TEST_F(QtViewportEdgeLengthHudTests,
       ReanchorsClippedLabelOnCameraPanZoomAndPaneResize) {
  const quader::ViewportOverlayFrame frame =
      hover_edge_frame(point_at_screen({200.0f, 300.0f}),
                       point_at_screen({2200.0f, 300.0f}));
  const std::uint64_t signature =
      quader::RenderSignatureBuilder::overlay_frame_signature(frame);
  const auto expect_follows_visible_edge = [&](const QRect &logical_bounds) {
    const quader::ViewportScreenPoint start =
        camera_.project_world_point(frame.lines.front().start);
    const quader::ViewportScreenPoint end =
        camera_.project_world_point(frame.lines.front().end);
    ASSERT_GT(start.position.x, 0.0f);
    ASSERT_LT(start.position.x, camera_.viewport_size().x);
    ASSERT_GT(end.position.x, camera_.viewport_size().x);
    hud_->present(frame, signature, camera_, logical_bounds, true, 7.0);
    expect_label_outside_at(
        {(start.position.x + camera_.viewport_size().x) * 0.5f,
         start.position.y},
        logical_bounds);
  };

  expect_follows_visible_edge(parent_->rect());
  const QString full_length = label_->text();
  camera_.begin_drag({400.0f, 300.0f}, quader::ViewportDragMode::Pan);
  camera_.update_drag({440.0f, 330.0f});
  camera_.end_drag();
  expect_follows_visible_edge(parent_->rect());
  camera_.scroll(1.0f);
  expect_follows_visible_edge(parent_->rect());
  camera_.set_viewport_size(987.0f, 743.0f);
  expect_follows_visible_edge({11, 17, 319, 203});
  EXPECT_EQ(label_->text(), full_length);
}

TEST_F(QtViewportEdgeLengthHudTests,
       HidesOffscreenClippedAndInvalidEdgesWithoutStaleText) {
  present(hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}));
  ASSERT_FALSE(label_->isHidden());
  present(hover_edge_frame({2000.0f, -1.0f, 0.0f},
                          {2000.0f, 1.0f, 0.0f}));
  EXPECT_TRUE(label_->isHidden());

  const quader::ViewportCameraPose pose = camera_.pose();
  const quader::QVec3 behind =
      pose.eye - quader::normalize(pose.target - pose.eye);
  present(hover_edge_frame(behind + quader::QVec3{-1.0f, 0.0f, 0.0f},
                          behind + quader::QVec3{1.0f, 0.0f, 0.0f}));
  EXPECT_TRUE(label_->isHidden());
  present(hover_edge_frame({}, {}));
  EXPECT_TRUE(label_->isHidden());
  present(hover_edge_frame({}, {std::numeric_limits<float>::quiet_NaN(),
                                0.0f, 0.0f}));
  EXPECT_TRUE(label_->isHidden());

  const quader::ViewportOverlayFrame visible =
      hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  present(visible);
  ASSERT_FALSE(label_->isHidden());
  camera_.set_clip_planes(0.05f, 10.0f);
  present(visible);
  EXPECT_TRUE(label_->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       StableFramesAvoidRepeatedGeometryVisibilityAndStackingWork) {
  EdgeHudWidgetEventCounter events;
  label_->installEventFilter(&events);
  quader::ViewportOverlayFrame frame =
      hover_edge_frame(point_at_screen({200.0f, 300.0f}),
                       point_at_screen({2200.0f, 300.0f}));
  present(frame);
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts initial = events.counts();
  const int initial_mask_revision =
      label_->property("quaderDimensionMaskRevision").toInt();
  ASSERT_GT(initial.show, 0);
  const QString original_text = label_->text();
  for (int repetition = 0; repetition < 64; ++repetition) {
    present(frame);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), initial);

  // An unrelated volatile overlay change must not touch the retained label.
  frame.points.push_back({
      .layer = quader::ViewportOverlayLayer::SelectedVertex,
      .position = {2.0f, 3.0f, 4.0f},
  });
  present(frame);
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), initial);

  frame.lines.front().start.x += 2.0f;
  frame.lines.front().end.x += 2.0f;
  present(frame);
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts moved = events.counts();
  EXPECT_GT(moved.move, initial.move);
  EXPECT_EQ(moved.show, initial.show);
  EXPECT_EQ(moved.hide, initial.hide);
  EXPECT_EQ(moved.z_order, initial.z_order);
  EXPECT_EQ(label_->property("quaderDimensionMaskRevision").toInt(),
            initial_mask_revision)
      << "Position-only HUD motion must not call QWidget::setMask().";
  EXPECT_EQ(label_->text(), original_text);

  // A real text/size change refreshes the native window region exactly once.
  frame.lines.front().end.x += 4.0f;
  present(frame);
  QCoreApplication::processEvents();
  EXPECT_EQ(label_->property("quaderDimensionMaskRevision").toInt(),
            initial_mask_revision + 1);

  // Moving a pane between monitors must rerasterize and reapply the native
  // mask even when its text, nominal font, and logical size stay unchanged.
  QEvent screen_change{QEvent::ScreenChangeInternal};
  QCoreApplication::sendEvent(label_, &screen_change);
  present(frame);
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts dpi_refreshed = events.counts();
  EXPECT_EQ(label_->property("quaderDimensionMaskRevision").toInt(),
            initial_mask_revision + 2);

  hud_->hide();
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts hidden = events.counts();
  EXPECT_GT(hidden.hide, dpi_refreshed.hide);
  for (int repetition = 0; repetition < 64; ++repetition) {
    present(frame, false);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), hidden);
  present(frame);
  EXPECT_FALSE(label_->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       PaneResizeRetiresNativeDimensionsBeforeFreshProjection) {
  ASSERT_TRUE(camera_.restore_perspective_pose(
      {.eye = {8, 6, 10}, .target = {}, .up = {0, 1, 0}}));
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, -3}, .max = {2, 3, 1}};
  const std::array pane_sizes{QSize{640, 420}, QSize{260, 300},
                              QSize{400, 300}};
  const std::array scales{1.0F, 2.0F, 1.0F};
  for (std::size_t scale_index = 0U; scale_index < scales.size(); ++scale_index) {
    hud_->apply_ui_scale(quader::QtUiScaleMetrics{
        scales[scale_index], scale_index + 1U});
    present_selection(bounds);
    ASSERT_TRUE(std::any_of(
        selection_labels_.begin(), selection_labels_.end(),
        [](const QLabel *label) { return !label->isHidden(); }));
    for (const QSize &pane_size : pane_sizes) {
      parent_->resize(pane_size);
      for (const QLabel *label : selection_labels_) {
        EXPECT_TRUE(label->isHidden())
            << "A resized pane must not expose its previous native projection.";
      }
      QCoreApplication::processEvents();
      for (const QLabel *label : selection_labels_) {
        EXPECT_TRUE(label->isHidden());
      }
      camera_.set_viewport_size(static_cast<float>(pane_size.width() * 2),
                                 static_cast<float>(pane_size.height() * 2));
      present_selection(bounds);
      for (const QLabel *label : selection_labels_) {
        ASSERT_FALSE(label->isHidden());
        EXPECT_TRUE(parent_->rect().contains(label->geometry()));
        EXPECT_EQ(label->text(), QStringLiteral("128"));
        EXPECT_FALSE(label->mask().isEmpty());
        EXPECT_FALSE(label->mask() == QRegion(label->rect()));
      }
    }
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       PaneMoveRetiresDimensionsWithoutChangingTheirRaster) {
  const auto frame = hover_edge_frame({-2, 0, 0}, {2, 0, 0});
  present(frame);
  ASSERT_FALSE(label_->isHidden());
  const int mask_revision =
      label_->property("quaderDimensionMaskRevision").toInt();
  const QRect geometry = label_->geometry();
  parent_->move(parent_->pos() + QPoint{35, 25});
  EXPECT_TRUE(label_->isHidden());
  present(frame);
  EXPECT_FALSE(label_->isHidden());
  EXPECT_EQ(label_->geometry(), geometry);
  EXPECT_EQ(label_->property("quaderDimensionMaskRevision").toInt(),
            mask_revision);
}

TEST_F(QtViewportEdgeLengthHudTests,
       AlienAncestorGeometryRetiresDimensionsWithUnchangedPaneBounds) {
  const ScopedExplicitHudNativeWidgets native_widgets;
  QWidget window;
  QWidget outer(&window);
  QWidget ancestor(&outer);
  QWidget pane(&ancestor);
  pane.setAttribute(Qt::WA_DontCreateNativeAncestors);
  window.setAttribute(Qt::WA_DontShowOnScreen);
  window.resize(800, 600);
  outer.setGeometry(30, 25, 700, 500);
  ancestor.setGeometry(35, 30, 600, 420);
  pane.setGeometry(20, 20, 400, 300);
  auto *pane_hud = new quader::QtViewportEdgeLengthHud(
      pane, quader::ViewportPaneId::Perspective);
  QLabel *label = pane.findChild<QLabel *>(
      QStringLiteral("viewportEdgeLength_perspective"));
  ASSERT_NE(label, nullptr);
  const auto frame = hover_edge_frame({-2, 0, 0}, {2, 0, 0});
  const auto present_projection = [&] {
    pane_hud->present(
        frame, quader::RenderSignatureBuilder::overlay_frame_signature(frame),
        camera_, pane.rect(), true, 12.0);
  };
  window.show();
  QCoreApplication::processEvents();
  present_projection();
  QCoreApplication::processEvents();
  ASSERT_FALSE(label->isHidden());
  ASSERT_EQ(outer.internalWinId(), WId{});
  ASSERT_EQ(ancestor.internalWinId(), WId{});
  const QRect pane_bounds = pane.geometry();
  const QRect label_bounds = label->geometry();
  const int mask_revision =
      label->property("quaderDimensionMaskRevision").toInt();

  for (QWidget *moving_ancestor : {&ancestor, &outer}) {
    moving_ancestor->move(moving_ancestor->pos() + QPoint{15, 0});
    EXPECT_EQ(pane.geometry(), pane_bounds);
    EXPECT_TRUE(label->isHidden())
        << "An alien origin change must retire the old native projection.";
    present_projection();
    ASSERT_FALSE(label->isHidden());
    EXPECT_EQ(label->geometry(), label_bounds);
    EXPECT_EQ(label->property("quaderDimensionMaskRevision").toInt(),
              mask_revision);

    moving_ancestor->resize(moving_ancestor->size() + QSize{10, 5});
    EXPECT_EQ(pane.geometry(), pane_bounds);
    EXPECT_TRUE(label->isHidden());
    present_projection();
    ASSERT_FALSE(label->isHidden());

    moving_ancestor->hide();
    EXPECT_TRUE(label->isHidden());
    moving_ancestor->show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(label->isHidden())
        << "Ancestor exposure must wait for a fresh projection.";
    present_projection();
    ASSERT_FALSE(label->isHidden());
    EXPECT_EQ(pane.geometry(), pane_bounds);
  }

  EdgeHudWidgetEventCounter events;
  label->installEventFilter(&events);
  QCoreApplication::processEvents();
  const auto stable = events.counts();
  for (int unchanged = 0; unchanged < 32; ++unchanged) {
    present_projection();
    ancestor.setGeometry(ancestor.geometry());
    outer.setGeometry(outer.geometry());
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), stable);
  EXPECT_EQ(label->property("quaderDimensionMaskRevision").toInt(),
            mask_revision);
  EXPECT_EQ(outer.internalWinId(), WId{});
  EXPECT_EQ(ancestor.internalWinId(), WId{});
  label->removeEventFilter(&events);
}

TEST_F(QtViewportEdgeLengthHudTests,
       ReparentedPaneAndAlienAncestorObserveOnlyTheirCurrentGeometryChain) {
  const ScopedExplicitHudNativeWidgets native_widgets;
  QWidget window;
  QWidget old_origin(&window);
  QWidget new_origin(&window);
  QWidget ancestor(&old_origin);
  QWidget pane;
  pane.setAttribute(Qt::WA_DontCreateNativeAncestors);
  window.setAttribute(Qt::WA_DontShowOnScreen);
  pane.setAttribute(Qt::WA_DontShowOnScreen);
  window.resize(1000, 600);
  old_origin.setGeometry(20, 20, 450, 500);
  new_origin.setGeometry(500, 20, 450, 500);
  ancestor.setGeometry(10, 10, 420, 400);
  pane.resize(400, 300);
  auto *pane_hud = new quader::QtViewportEdgeLengthHud(
      pane, quader::ViewportPaneId::Perspective);
  QLabel *label = pane.findChild<QLabel *>(
      QStringLiteral("viewportEdgeLength_perspective"));
  ASSERT_NE(label, nullptr);
  const auto frame = hover_edge_frame({-2, 0, 0}, {2, 0, 0});
  const auto present_projection = [&] {
    pane_hud->present(
        frame, quader::RenderSignatureBuilder::overlay_frame_signature(frame),
        camera_, pane.rect(), true, 12.0);
  };
  window.show();
  pane.show();
  QCoreApplication::processEvents();
  present_projection();
  ASSERT_FALSE(label->isHidden());
  pane.setParent(&ancestor);
  EXPECT_TRUE(label->isHidden());
  pane.move(0, 0);
  pane.show();
  QCoreApplication::processEvents();
  present_projection();
  ASSERT_FALSE(label->isHidden());
  ASSERT_EQ(ancestor.internalWinId(), WId{});
  ASSERT_EQ(old_origin.internalWinId(), WId{});
  old_origin.move(old_origin.pos() + QPoint{15, 0});
  EXPECT_TRUE(label->isHidden());
  present_projection();
  ASSERT_FALSE(label->isHidden());

  ancestor.setParent(&new_origin);
  EXPECT_TRUE(label->isHidden());
  ancestor.show();
  pane.show();
  QCoreApplication::processEvents();
  present_projection();
  ASSERT_FALSE(label->isHidden());
  ASSERT_EQ(new_origin.internalWinId(), WId{});
  const QRect pane_bounds = pane.geometry();
  const int mask_revision =
      label->property("quaderDimensionMaskRevision").toInt();
  EdgeHudWidgetEventCounter events;
  label->installEventFilter(&events);
  QCoreApplication::processEvents();
  const auto stable = events.counts();
  old_origin.move(old_origin.pos() + QPoint{15, 0});
  for (int unchanged = 0; unchanged < 32; ++unchanged) present_projection();
  QCoreApplication::processEvents();
  EXPECT_FALSE(label->isHidden());
  EXPECT_EQ(events.counts(), stable)
      << "The retired ancestor chain must no longer invalidate the HUD.";
  new_origin.move(new_origin.pos() + QPoint{15, 0});
  EXPECT_EQ(pane.geometry(), pane_bounds);
  EXPECT_TRUE(label->isHidden());
  present_projection();
  EXPECT_FALSE(label->isHidden());
  EXPECT_EQ(label->property("quaderDimensionMaskRevision").toInt(),
            mask_revision);
  label->removeEventFilter(&events);
}

TEST_F(QtViewportEdgeLengthHudTests,
       NativeAncestorChangesEraseCurrentAndHistoricalBackingStoreGlyphs) {
#if defined(_WIN32)
  if (QGuiApplication::platformName() != QStringLiteral("windows") ||
      qEnvironmentVariableIntValue("QUADER_RUN_NATIVE_DIMENSION_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_NATIVE_DIMENSION_TESTS=1 for native raster checks";
  }
  // Native filters run newest first. This observer precedes the new HUD so its
  // callback inspects retirement after that HUD and before Windows moves pixels.
  EdgeHudNativeGeometryObserver observer;
  QWidget window;
  QWidget ancestor(&window);
  QWidget pane(&ancestor);
  QPalette palette = window.palette();
  palette.setColor(QPalette::Window, QColor{23, 24, 25});
  for (QWidget *widget : {&window, &ancestor, &pane}) {
    widget->setPalette(palette);
    widget->setAutoFillBackground(true);
  }
  window.resize(800, 600);
  ancestor.setGeometry(60, 50, 620, 440);
  pane.setGeometry(20, 20, 460, 340);
  window.show();
  QCoreApplication::processEvents();
  auto *pane_hud = new quader::QtViewportEdgeLengthHud(
      pane, quader::ViewportPaneId::Perspective);
  std::array<QLabel *, 3> labels{};
  std::array<QColor, 3> colors{};
  std::array<EdgeHudWidgetEventCounter, 3> pixel_events;
  const std::array axis_names{QStringLiteral("X"), QStringLiteral("Y"),
                              QStringLiteral("Z")};
  for (std::size_t axis = 0U; axis < labels.size(); ++axis) {
    labels[axis] = pane.findChild<QLabel *>(
        QStringLiteral("viewportSelectionDimension%1_perspective")
            .arg(axis_names[axis]));
    ASSERT_NE(labels[axis], nullptr);
    colors[axis] = labels[axis]->palette().color(labels[axis]->foregroundRole());
    labels[axis]->installEventFilter(&pixel_events[axis]);
  }
  camera_.focus_origin(16.0F);
  camera_.view_axis(quader::ViewportViewAxis::Z, 1);
  const auto present_bounds = [&](quader::QVec2 anchor,
                                   std::uint64_t signature) {
    camera_.set_viewport_size(static_cast<float>(pane.width() * 2),
                               static_cast<float>(pane.height() * 2));
    const quader::QVec3 midpoint = point_at_screen(anchor);
    const quader::ViewportSelectionDimensionBounds bounds{
        .min = midpoint - quader::QVec3{2, 0, 0},
        .max = midpoint + quader::QVec3{2, 4, 4}};
    pane_hud->present({}, 1U, camera_, pane.rect(), false, 12.0, nullptr,
                       false, &bounds, signature, true);
  };
  const auto current_pixel_bounds = [&] {
    QRect bounds;
    for (const QLabel *label : labels) {
      if (!label->isHidden()) {
        bounds = bounds.united(native_label_pixel_bounds(*label, window));
      }
    }
    return bounds;
  };
  const auto paint_accepted_projection = [&] {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::UpdateRequest);
    QCoreApplication::processEvents();
    for (QLabel *label : labels) {
      if (!label->isHidden()) {
        // This establishes the accepted BEFORE sample only. Retirement reads
        // never invoke painting or capture APIs, so residue stays observable.
        ASSERT_TRUE(label->isVisible());
        label->repaint();
      }
    }
  };
  std::uint64_t signature = 0U;
  present_bounds({460, 340}, ++signature);
  paint_accepted_projection();
  ASSERT_FALSE(labels[0]->isHidden());
  ASSERT_NE(native_backing_image(window), nullptr);
  const QRect current_bounds = current_pixel_bounds();
  const QImage natural_before = native_backing_image(window)->copy();
  ASSERT_GT(native_dimension_pixel_count(natural_before, current_bounds, colors), 8)
      << "Actual label Paint count=" << pixel_events[0].counts().paint
      << ", all-store glyph pixels="
      << native_dimension_pixel_count(natural_before, natural_before.rect(), colors)
      << ", store=" << natural_before.width() << 'x' << natural_before.height()
      << ", store DPR=" << natural_before.devicePixelRatio()
      << ", window DPR=" << window.devicePixelRatioF()
      << ", footprint=" << current_bounds.x() << ',' << current_bounds.y()
      << ' ' << current_bounds.width() << 'x' << current_bounds.height();
  const HWND ancestor_handle =
      reinterpret_cast<HWND>(ancestor.internalWinId());
  HWND observed_handle = ancestor_handle;
  RECT old_native_geometry{};
  ASSERT_TRUE(GetWindowRect(ancestor_handle, &old_native_geometry));
  int observed_changes = 0;
  const auto observe_retirement = [&](const MSG &message,
                                       const QRect &expected_clean_bounds) {
    if (message.hwnd != observed_handle ||
        message.message != WM_WINDOWPOSCHANGING || message.lParam == 0) {
      return;
    }
    const auto &position =
        *reinterpret_cast<const WINDOWPOS *>(message.lParam);
    if ((position.flags & (SWP_NOMOVE | SWP_NOSIZE)) ==
        (SWP_NOMOVE | SWP_NOSIZE)) {
      return;
    }
    ++observed_changes;
    RECT still_old{};
    ASSERT_TRUE(GetWindowRect(observed_handle, &still_old));
    EXPECT_EQ(still_old.left, old_native_geometry.left);
    EXPECT_EQ(still_old.top, old_native_geometry.top);
    EXPECT_EQ(still_old.right, old_native_geometry.right);
    EXPECT_EQ(still_old.bottom, old_native_geometry.bottom);
    for (const QLabel *label : labels) {
      EXPECT_TRUE(label->isHidden());
    }
    const QImage retired = native_backing_image(window)->copy();
    EXPECT_EQ(native_dimension_pixel_count(retired, expected_clean_bounds,
                                            colors), 0)
        << "Accepted glyph pixels must disappear before native geometry changes.";
  };
  observer.set_observer([&](const MSG &message) {
    observe_retirement(message, current_bounds);
  });
  RECT native_position = old_native_geometry;
  MapWindowPoints(HWND_DESKTOP, GetParent(ancestor_handle),
                  reinterpret_cast<POINT *>(&native_position), 2);
  ASSERT_TRUE(SetWindowPos(ancestor_handle, nullptr, native_position.left + 25,
                           native_position.top + 20, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  observer.set_observer({});
  EXPECT_GT(observed_changes, 0);
  QCoreApplication::processEvents();
  present_bounds({460, 340}, ++signature);
  ASSERT_FALSE(labels[0]->isHidden());

  // Top-level WINDOWPOS coordinates are screen coordinates, independent of
  // the child-client branch exercised above.
  paint_accepted_projection();
  const QRect top_level_bounds = current_pixel_bounds();
  const QImage top_level_before = native_backing_image(window)->copy();
  ASSERT_GT(native_dimension_pixel_count(top_level_before, top_level_bounds,
                                          colors), 8);
  observed_handle = reinterpret_cast<HWND>(window.internalWinId());
  ASSERT_TRUE(GetWindowRect(observed_handle, &old_native_geometry));
  observed_changes = 0;
  observer.set_observer([&](const MSG &message) {
    observe_retirement(message, top_level_bounds);
  });
  ASSERT_TRUE(SetWindowPos(observed_handle, nullptr,
                           old_native_geometry.left + 25,
                           old_native_geometry.top + 20, 0, 0,
                           SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
  observer.set_observer({});
  EXPECT_GT(observed_changes, 0);
  QCoreApplication::processEvents();
  present_bounds({460, 340}, ++signature);
  ASSERT_FALSE(labels[0]->isHidden());
  observed_handle = ancestor_handle;

  const std::array scales{1.0F, 2.0F, 1.0F};
  const std::array sizes{QSize{460, 340}, QSize{300, 300}, QSize{460, 340}};
  for (std::size_t scale_index = 0U; scale_index < scales.size(); ++scale_index) {
    SCOPED_TRACE(scales[scale_index]);
    pane.resize(sizes[scale_index]);
    pane_hud->apply_ui_scale(quader::QtUiScaleMetrics{
        scales[scale_index], scale_index + 1U});
    // Samples come only from real accepted glyph exposures. Restoring these
    // earlier patches models native raster residue deterministically, without
    // relying on compositor timing or invoking a capture that repaints it away.
    std::vector<std::pair<QRect, QImage>> history;
    std::array<QRect, 3> x_exposures;
    QRect historical_bounds;
    for (std::size_t position_index = 0U; position_index < 3U; ++position_index) {
      const float fraction = 0.2F + static_cast<float>(position_index) * 0.3F;
      present_bounds({pane.width() * 2.0F * fraction,
                       pane.height() * 2.0F * fraction}, ++signature);
      paint_accepted_projection();
      ASSERT_FALSE(labels[0]->isHidden());
      x_exposures[position_index] = native_label_pixel_bounds(*labels[0], window);
      QImage *backing = native_backing_image(window);
      ASSERT_NE(backing, nullptr);
      for (const QLabel *label : labels) {
        if (label->isHidden()) continue;
        const QRect bounds = native_label_pixel_bounds(*label, window);
        ASSERT_GT(native_dimension_pixel_count(*backing, bounds, colors), 8);
        history.emplace_back(bounds, backing->copy(bounds));
        historical_bounds = historical_bounds.united(bounds);
      }
    }
    EXPECT_FALSE(x_exposures[0].intersects(x_exposures[1]));
    EXPECT_FALSE(x_exposures[1].intersects(x_exposures[2]));
    window.setUpdatesEnabled(false);
    pane_hud->hide();
    for (const QLabel *label : labels) EXPECT_TRUE(label->isHidden());
    present_bounds({static_cast<float>(pane.width()),
                     static_cast<float>(pane.height())}, ++signature);
    for (const QLabel *label : labels) {
      EXPECT_TRUE(label->isHidden())
          << "Pending native cleanup must finish before another projection appears.";
    }
    window.setUpdatesEnabled(true);
    {
      QImage *backing = native_backing_image(window);
      ASSERT_NE(backing, nullptr);
      ASSERT_EQ(backing->depth(), 32);
      // Copy physical samples directly; a painter would apply the image's
      // device-pixel transform a second time on a scaled Windows monitor.
      for (const auto &[bounds, pixels] : history) {
        ASSERT_TRUE(backing->rect().contains(bounds));
        for (int y = 0; y < bounds.height(); ++y) {
          std::copy_n(
              reinterpret_cast<const QRgb *>(pixels.constScanLine(y)),
              bounds.width(),
              reinterpret_cast<QRgb *>(backing->scanLine(bounds.top() + y)) +
                  bounds.left());
        }
      }
    }
    const QImage historical_before = native_backing_image(window)->copy();
    for (const auto &[bounds, pixels] : history) {
      (void)pixels;
      ASSERT_GT(native_dimension_pixel_count(historical_before, bounds, colors), 8);
    }
    ASSERT_TRUE(GetWindowRect(ancestor_handle, &old_native_geometry));
    observed_changes = 0;
    observer.set_observer([&](const MSG &message) {
      observe_retirement(message, historical_bounds);
    });
    // QWidget changes its logical position before sending the native message;
    // cached top-level footprints must still erase the former coordinates.
    ancestor.setGeometry(ancestor.geometry().translated(12, 9).adjusted(
        0, 0, scale_index % 2U == 0U ? 10 : -10, 0));
    observer.set_observer({});
    EXPECT_GT(observed_changes, 0);
    const QImage historical_after = native_backing_image(window)->copy();
    EXPECT_EQ(native_dimension_pixel_count(historical_after, historical_bounds,
                                            colors), 0);
    QCoreApplication::processEvents();
    present_bounds({static_cast<float>(pane.width()),
                     static_cast<float>(pane.height())}, ++signature);
    ASSERT_FALSE(labels[0]->isHidden());
    EXPECT_EQ(labels[0]->text(), QStringLiteral("128"));
    EXPECT_TRUE(pane.rect().contains(labels[0]->geometry()));
    const int mask_revision =
        labels[0]->property("quaderDimensionMaskRevision").toInt();
    EdgeHudWidgetEventCounter retained_events;
    labels[0]->installEventFilter(&retained_events);
    QCoreApplication::processEvents();
    const auto stable = retained_events.counts();
    for (int unchanged = 0; unchanged < 32; ++unchanged) {
      present_bounds({static_cast<float>(pane.width()),
                       static_cast<float>(pane.height())}, signature);
    }
    QCoreApplication::processEvents();
    EXPECT_EQ(retained_events.counts(), stable);
    EXPECT_EQ(labels[0]->property("quaderDimensionMaskRevision").toInt(),
              mask_revision);
    ASSERT_TRUE(SetWindowPos(ancestor_handle, nullptr, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                                 SWP_NOACTIVATE));
    EXPECT_FALSE(labels[0]->isHidden())
        << "No-op native window changes must retain the current projection.";

    EdgeHudWidgetEventCounter window_events;
    window.installEventFilter(&window_events);
    const auto before_identical_position = retained_events.counts();
    RECT identical_geometry{};
    ASSERT_TRUE(GetWindowRect(ancestor_handle, &identical_geometry));
    MapWindowPoints(HWND_DESKTOP, GetParent(ancestor_handle),
                    reinterpret_cast<POINT *>(&identical_geometry), 2);
    ASSERT_TRUE(SetWindowPos(ancestor_handle, nullptr, identical_geometry.left,
                             identical_geometry.top,
                             identical_geometry.right - identical_geometry.left,
                             identical_geometry.bottom - identical_geometry.top,
                             SWP_NOZORDER | SWP_NOACTIVATE));
    EXPECT_FALSE(labels[0]->isHidden());
    EXPECT_EQ(retained_events.counts(), before_identical_position);
    EXPECT_EQ(window_events.counts().paint, 0)
        << "An identical proposed rectangle must not synchronously repaint history.";
    EXPECT_EQ(labels[0]->property("quaderDimensionMaskRevision").toInt(),
              mask_revision);
    window.removeEventFilter(&window_events);
    labels[0]->removeEventFilter(&retained_events);

    present_bounds({pane.width() * 0.4F, pane.height() * 0.4F}, ++signature);
    paint_accepted_projection();
    const QRect unmapped_bounds = current_pixel_bounds();
    const QImage mapped_before = native_backing_image(window)->copy();
    ASSERT_GT(native_dimension_pixel_count(mapped_before, unmapped_bounds,
                                            colors), 8);
    // Qt discards synchronous UpdateNow paints while the top-level widget is
    // unmapped. Toggle the public state directly to avoid platform-dependent
    // minimize timing while preserving the actual native backing image.
    window.setAttribute(Qt::WA_Mapped, false);
    pane_hud->hide();
    present_bounds({pane.width() * 1.6F, pane.height() * 1.6F}, ++signature);
    for (const QLabel *label : labels) EXPECT_TRUE(label->isHidden());
    const QImage unmapped_after = native_backing_image(window)->copy();
    EXPECT_GT(native_dimension_pixel_count(unmapped_after, unmapped_bounds,
                                            colors), 8)
        << "An unmapped root must retain pending pixels until painting can resume.";
    window.setAttribute(Qt::WA_Mapped, true);
    int observed_reveals = 0;
    observer.set_observer([&](const MSG &message) {
      if (message.hwnd != reinterpret_cast<HWND>(labels[0]->internalWinId()) ||
          message.message != WM_WINDOWPOSCHANGING || message.lParam == 0) {
        return;
      }
      const auto &position =
          *reinterpret_cast<const WINDOWPOS *>(message.lParam);
      if ((position.flags & SWP_SHOWWINDOW) == 0) return;
      ++observed_reveals;
      const QImage before_reveal = native_backing_image(window)->copy();
      EXPECT_EQ(native_dimension_pixel_count(before_reveal, unmapped_bounds,
                                              colors), 0)
          << "Pending unmapped pixels must clear before a fresh native reveal.";
    });
    present_bounds({pane.width() * 1.6F, pane.height() * 1.6F}, ++signature);
    observer.set_observer({});
    EXPECT_GT(observed_reveals, 0);
    EXPECT_FALSE(labels[0]->isHidden());

    pane.hide();
    for (const QLabel *label : labels) EXPECT_TRUE(label->isHidden());
    pane.show();
    QCoreApplication::processEvents();
    for (const QLabel *label : labels) EXPECT_TRUE(label->isHidden());
    present_bounds({static_cast<float>(pane.width()),
                     static_cast<float>(pane.height())}, ++signature);
    EXPECT_FALSE(labels[0]->isHidden());
  }
#else
  GTEST_SKIP() << "Native backing-store raster checks require Windows.";
#endif
}

TEST_F(QtViewportEdgeLengthHudTests,
       PaneReactivationWaitsForFreshDimensionPresentation) {
  const auto frame = hover_edge_frame({-2, 0, 0}, {2, 0, 0});
  present(frame);
  ASSERT_FALSE(label_->isHidden());
  const QRect accepted_geometry = label_->geometry();
  const QRegion accepted_mask = label_->mask();
  const int accepted_mask_revision =
      label_->property("quaderDimensionMaskRevision").toInt();
  for (int repetition = 0; repetition < 4; ++repetition) {
    parent_->hide();
    EXPECT_TRUE(label_->isHidden());
    parent_->show();
    QCoreApplication::processEvents();
    EXPECT_TRUE(label_->isHidden())
        << "Showing a pane must not reveal dimensions from its prior exposure.";
    present(frame);
    EXPECT_FALSE(label_->isHidden());
    EXPECT_EQ(label_->geometry(), accepted_geometry);
    EXPECT_EQ(label_->mask(), accepted_mask);
    EXPECT_EQ(label_->property("quaderDimensionMaskRevision").toInt(),
              accepted_mask_revision);
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionDimensionsUseAxisColorsAndClosestVisibleCornerInPerspective) {
  ASSERT_TRUE(camera_.restore_perspective_pose(
      {.eye = {8, 6, 10}, .target = {}, .up = {0, 1, 0}}));
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, -3}, .max = {2, 3, 1}};
  present_selection(bounds);
  expect_selection_anchor(0U, {0, 3, 1});
  expect_selection_anchor(1U, {2, 1, 1});
  expect_selection_anchor(2U, {2, 3, -1});

  constexpr std::array kColors{quader::kOverlayToolAxisXColor,
                              quader::kOverlayToolAxisYColor,
                              quader::kOverlayToolAxisZColor};
  for (std::size_t axis = 0U; axis < selection_labels_.size(); ++axis) {
    QLabel &label = *selection_labels_[axis];
    EXPECT_EQ(label.text(), QStringLiteral("128"));
    const auto packed = kColors[axis];
    const QColor expected{static_cast<int>(packed & 0xffU),
                          static_cast<int>((packed >> 8U) & 0xffU),
                          static_cast<int>((packed >> 16U) & 0xffU),
                          static_cast<int>((packed >> 24U) & 0xffU)};
    EXPECT_EQ(label.palette().color(label.foregroundRole()), expected);
    QImage painted(label.size(), QImage::Format_ARGB32);
    painted.fill(Qt::transparent);
    label.render(&painted);
    int colored_pixels = 0;
    int outline_pixels = 0;
    for (int y = 0; y < painted.height(); ++y) {
      for (int x = 0; x < painted.width(); ++x) {
        const QColor pixel = painted.pixelColor(x, y);
        colored_pixels +=
            pixel.alpha() == expected.alpha() &&
                    std::abs(pixel.red() - expected.red()) <= 1 &&
                    std::abs(pixel.green() - expected.green()) <= 1 &&
                    std::abs(pixel.blue() - expected.blue()) <= 1
                ? 1
                : 0;
        outline_pixels += pixel == QColor(0, 0, 0, 255) ? 1 : 0;
      }
    }
    EXPECT_GT(colored_pixels, 8);
    EXPECT_GT(outline_pixels, 20);
  }

  // Camera-only motion selects a new corner without needing a geometry revision.
  ASSERT_TRUE(camera_.restore_perspective_pose(
      {.eye = {-8, -6, -10}, .target = {}, .up = {0, 1, 0}}));
  present_selection(bounds);
  expect_selection_anchor(0U, {0, -1, -3});
  expect_selection_anchor(1U, {-2, 1, -3});
  expect_selection_anchor(2U, {-2, -1, -1});
}

TEST_F(QtViewportEdgeLengthHudTests,
       PerspectiveSelectionFallsBackToMinimumCornerWhenAllCornersAreOffscreen) {
  ASSERT_TRUE(camera_.restore_perspective_pose(
      {.eye = {0, 0, 10}, .target = {}, .up = {0, 1, 0}}));
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-100, -1, 0}, .max = {100, 1, 2}};
  present_selection(bounds);
  expect_selection_anchor(0U, {0, -1, 0});
  EXPECT_EQ(selection_labels_[0]->text(), QStringLiteral("6400"));
  EXPECT_TRUE(selection_labels_[1]->isHidden());
  EXPECT_TRUE(selection_labels_[2]->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       OrthographicSelectionDimensionsOmitDepthAxisAndKeepMinimumCorner) {
  camera_.focus_origin(16.0F);
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, -3}, .max = {2, 3, 1}};
  const std::array view_axes{quader::ViewportViewAxis::X,
                             quader::ViewportViewAxis::Y,
                             quader::ViewportViewAxis::Z};
  const std::array midpoints{quader::QVec3{0, -1, -3},
                             quader::QVec3{-2, 1, -3},
                             quader::QVec3{-2, -1, -1}};
  for (std::size_t depth_axis = 0U; depth_axis < 3U; ++depth_axis) {
    for (const int sign : {-1, 1}) {
      camera_.view_axis(view_axes[depth_axis], sign);
      present_selection(bounds);
      for (std::size_t axis = 0U; axis < 3U; ++axis) {
        if (axis == depth_axis) {
          EXPECT_TRUE(selection_labels_[axis]->isHidden());
        } else {
          expect_selection_anchor(axis, midpoints[axis]);
        }
      }
    }
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionFormattingSuppressesTinySpansAndUsesIntegerTolerance) {
  const float world_per_map = static_cast<float>(
      1.0 / quader::viewport_host_defaults().map_units_per_world_unit);
  quader::ViewportSelectionDimensionBounds bounds{
      .min = {}, .max = {64.005F * world_per_map, 23.126F * world_per_map, 0}};
  present_selection(bounds);
  ASSERT_FALSE(selection_labels_[0]->isHidden());
  ASSERT_FALSE(selection_labels_[1]->isHidden());
  EXPECT_EQ(selection_labels_[0]->text(), QStringLiteral("64"));
  EXPECT_EQ(selection_labels_[1]->text(), QStringLiteral("23.13"));

  bounds.max.x = 0.009F * world_per_map;
  bounds.max.y = 64.02F * world_per_map;
  present_selection(bounds, 2U);
  EXPECT_TRUE(selection_labels_[0]->isHidden());
  ASSERT_FALSE(selection_labels_[1]->isHidden());
  EXPECT_EQ(selection_labels_[1]->text(), QStringLiteral("64.02"));
  bounds.max.x = 0.02F * world_per_map;
  present_selection(bounds, 3U);
  ASSERT_FALSE(selection_labels_[0]->isHidden());
  EXPECT_EQ(selection_labels_[0]->text(), QStringLiteral("0.02"));
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionToggleAndResetRemainIndependentOfHoverAndFootprint) {
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, 0}, .max = {2, 3, 0}};
  const auto frame = hover_edge_frame({-4, 0, 0}, {4, 0, 0});
  const quader::ModelingBoxPreview footprint{
      .drag_state = quader::ModelingBoxDragState::Footprint,
      .footprint = {quader::QVec3{-2, -3, 0}, quader::QVec3{2, -3, 0}},
      .footprint_count = 2U};
  const auto present_all = [&](bool selection_enabled) {
    hud_->present(frame, 1U, camera_, parent_->rect(), true, 12.0, &footprint,
                   true, &bounds, 1U, selection_enabled);
  };
  present_all(true);
  ASSERT_FALSE(label_->isHidden());
  ASSERT_FALSE(footprint_labels_[0]->isHidden());
  ASSERT_FALSE(selection_labels_[0]->isHidden());
  ASSERT_FALSE(selection_labels_[1]->isHidden());
  present_all(false);
  EXPECT_FALSE(label_->isHidden());
  EXPECT_FALSE(footprint_labels_[0]->isHidden());
  for (const QLabel *label : selection_labels_) EXPECT_TRUE(label->isHidden());
  present_all(true);
  EXPECT_FALSE(selection_labels_[0]->isHidden());
  hud_->hide();
  for (const QLabel *label : selection_labels_) EXPECT_TRUE(label->isHidden());
  present_selection(bounds);
  EXPECT_FALSE(selection_labels_[0]->isHidden());
  EXPECT_TRUE(label_->isHidden());
  EXPECT_TRUE(footprint_labels_[0]->isHidden());
  hud_->present({}, 1U, camera_, parent_->rect(), false, 12.0, nullptr, false,
                 nullptr, 2U, true);
  for (const QLabel *label : selection_labels_) EXPECT_TRUE(label->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionLabelsUseTrueMidpointsAndHideOffscreenOrClippedSpans) {
  // The X edge crosses the viewport but its true midpoint lies outside it.
  quader::ViewportSelectionDimensionBounds bounds{
      .min = {-10, -1, 0}, .max = {1000, 1, 0}};
  ASSERT_TRUE(camera_.project_visible_world_segment(bounds.min,
                                                    {1000, -1, 0}).has_value());
  const auto midpoint = camera_.project_world_point({495, -1, 0});
  ASSERT_TRUE(midpoint.visible);
  ASSERT_TRUE(midpoint.position.x < 0.0F ||
              midpoint.position.x > camera_.viewport_size().x);
  present_selection(bounds);
  EXPECT_TRUE(selection_labels_[0]->isHidden());
  EXPECT_FALSE(selection_labels_[1]->isHidden());

  bounds = {.min = {-2, -1, 0}, .max = {2, 3, 0}};
  present_selection(bounds, 2U);
  ASSERT_FALSE(selection_labels_[0]->isHidden());
  camera_.set_clip_planes(0.05F, 10.0F);
  present_selection(bounds, 2U);
  for (const QLabel *label : selection_labels_) EXPECT_TRUE(label->isHidden());

  bounds.max.x = std::numeric_limits<float>::quiet_NaN();
  present_selection(bounds, 3U);
  for (const QLabel *label : selection_labels_) EXPECT_TRUE(label->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionSignatureGatesBoundsFormattingAndNativeWidgetWork) {
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, 0}, .max = {2, 3, 0}};
  std::array<EdgeHudWidgetEventCounter, 3> events;
  for (std::size_t axis = 0U; axis < events.size(); ++axis) {
    selection_labels_[axis]->installEventFilter(&events[axis]);
  }
  present_selection(bounds);
  QCoreApplication::processEvents();
  const std::array initial{events[0].counts(), events[1].counts(),
                           events[2].counts()};
  const int mask_revision =
      selection_labels_[0]->property("quaderDimensionMaskRevision").toInt();
  const quader::ViewportSelectionDimensionBounds stale_input{
      .min = {1000, 1000, 1000}, .max = {2000, 2000, 2000}};
  for (int repetition = 0; repetition < 64; ++repetition) {
    present_selection(stale_input);
  }
  QCoreApplication::processEvents();
  for (std::size_t axis = 0U; axis < events.size(); ++axis) {
    EXPECT_EQ(events[axis].counts(), initial[axis]);
  }
  EXPECT_EQ(selection_labels_[0]->text(), QStringLiteral("128"));
  EXPECT_EQ(selection_labels_[0]->property("quaderDimensionMaskRevision").toInt(),
            mask_revision);

  camera_.view_axis(quader::ViewportViewAxis::X, 1);
  present_selection(stale_input);
  EXPECT_TRUE(selection_labels_[0]->isHidden());
  EXPECT_FALSE(selection_labels_[1]->isHidden());
  EXPECT_EQ(selection_labels_[1]->text(), QStringLiteral("128"));
  present_selection(stale_input, 2U);
  for (const QLabel *label : selection_labels_) EXPECT_TRUE(label->isHidden());
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionLabelsScaleWithinNarrowPanesAndCanCaptureNativePresentation) {
  ASSERT_TRUE(camera_.restore_perspective_pose(
      {.eye = {8, 6, 10}, .target = {}, .up = {0, 1, 0}}));
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, -3}, .max = {2, 3, 1}};
  QPalette palette = parent_->palette();
  palette.setColor(QPalette::Window, QColor(23, 24, 25));
  parent_->setPalette(palette);
  parent_->setAutoFillBackground(true);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  const auto capture = [&](const QString &name) {
    if (!review_directory.isEmpty()) {
      ASSERT_TRUE(QDir{}.mkpath(review_directory));
      ASSERT_TRUE(parent_->grab().save(QDir(review_directory).filePath(name)));
    }
  };
  present_selection(bounds);
  const QSize baseline = selection_labels_[0]->size();
  const std::array baseline_geometry{selection_labels_[0]->geometry(),
                                     selection_labels_[1]->geometry(),
                                     selection_labels_[2]->geometry()};
  capture(QStringLiteral("selection-dimensions-100.png"));
  hud_->apply_ui_scale(quader::QtUiScaleMetrics{2.0F, 1U});
  present_selection(bounds);
  EXPECT_GT(selection_labels_[0]->width(), baseline.width());
  EXPECT_DOUBLE_EQ(selection_labels_[0]->font().pointSizeF(), 24.0);
  capture(QStringLiteral("selection-dimensions-200.png"));
  parent_->resize(180, 220);
  camera_.set_viewport_size(360, 440);
  present_selection(bounds);
  for (const QLabel *label : selection_labels_) {
    ASSERT_FALSE(label->isHidden());
    EXPECT_TRUE(parent_->rect().contains(label->geometry()));
  }
  for (std::size_t axis = 0U; axis < selection_labels_.size(); ++axis) {
    for (std::size_t other = axis + 1U; other < selection_labels_.size(); ++other) {
      EXPECT_FALSE(selection_labels_[axis]->geometry().intersects(
          selection_labels_[other]->geometry()));
    }
  }
  capture(QStringLiteral("selection-dimensions-narrow-200.png"));
  hud_->apply_ui_scale(quader::QtUiScaleMetrics{1.0F, 2U});
  present_selection(bounds);
  EXPECT_EQ(selection_labels_[0]->size(), baseline);
  capture(QStringLiteral("selection-dimensions-narrow-100.png"));
  parent_->resize(400, 300);
  camera_.set_viewport_size(800, 600);
  present_selection(bounds);
  for (std::size_t axis = 0U; axis < selection_labels_.size(); ++axis) {
    EXPECT_EQ(selection_labels_[axis]->geometry(), baseline_geometry[axis]);
  }
}

TEST_F(QtViewportEdgeLengthHudTests,
       SelectionLabelsHideWhenNoNonOverlappingPositionFits) {
  ASSERT_TRUE(camera_.restore_perspective_pose(
      {.eye = {8, 6, 10}, .target = {}, .up = {0, 1, 0}}));
  const quader::ViewportSelectionDimensionBounds bounds{
      .min = {-2, -1, -3}, .max = {2, 3, 1}};
  hud_->apply_ui_scale(quader::QtUiScaleMetrics{2.0F, 1U});
  present_selection(bounds);
  const QSize label_size = selection_labels_[0]->size();
  parent_->resize(label_size + QSize{2, 2});
  camera_.set_viewport_size(static_cast<float>(parent_->width() * 2),
                             static_cast<float>(parent_->height() * 2));
  present_selection(bounds);
  EXPECT_EQ(std::count_if(selection_labels_.begin(), selection_labels_.end(),
                          [](const QLabel *label) { return !label->isHidden(); }),
            1);
  parent_->resize(400, 300);
  camera_.set_viewport_size(800, 600);
  present_selection(bounds);
  for (const QLabel *label : selection_labels_) EXPECT_FALSE(label->isHidden());
}

} // namespace
