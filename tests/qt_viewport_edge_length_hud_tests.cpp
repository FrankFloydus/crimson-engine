////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "render/render_signature_builder.hpp"
#include "ui/qt_viewport_edge_length_hud.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QEnterEvent>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPalette>
#include <QPoint>
#include <QString>
#include <QWidget>

#include <cmath>
#include <limits>
#include <memory>

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
      default:
        break;
      }
    }
    return QObject::eventFilter(watched, event);
  }

private:
  Counts counts_;
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
    camera_.focus_origin(64.0f);
    camera_.view_axis(quader::ViewportViewAxis::Z, 1);
    camera_.set_viewport_size(800.0f, 600.0f);
  }

  void present(const quader::ViewportOverlayFrame &frame, bool enabled = true) {
    hud_->present(
        frame, quader::RenderSignatureBuilder::overlay_frame_signature(frame),
        camera_, parent_->rect(), enabled);
  }

  int argc_ = 1;
  char application_name_[33] = "quader-edge-length-hud-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
  std::unique_ptr<QWidget> parent_;
  quader::QtViewportEdgeLengthHud *hud_ = nullptr;
  QLabel *label_ = nullptr;
  quader::ViewportCameraController camera_;
};

TEST_F(QtViewportEdgeLengthHudTests,
       UsesFullWorldSpaceLengthInMapUnitsAndPaletteBacking) {
  // The 3D length is 13; its screen-plane length is only 5. World endpoints
  // already carry object, instance, and hierarchy transforms from projection.
  present(hover_edge_frame({-1.5f, -2.0f, -6.0f}, {1.5f, 2.0f, 6.0f}));

  ASSERT_FALSE(label_->isHidden());
  EXPECT_EQ(label_->text(),
            QString::number(
                13.0 * quader::viewport_host_defaults().map_units_per_world_unit,
                'f', 0));
  EXPECT_TRUE(label_->autoFillBackground());
  EXPECT_EQ(label_->backgroundRole(), QPalette::ToolTipBase);
  EXPECT_EQ(label_->foregroundRole(), QPalette::ToolTipText);
  EXPECT_TRUE(label_->testAttribute(Qt::WA_NativeWindow));
  EXPECT_TRUE(label_->testAttribute(Qt::WA_TransparentForMouseEvents));
  EXPECT_EQ(label_->focusPolicy(), Qt::NoFocus);
}

TEST_F(QtViewportEdgeLengthHudTests, FormatsCompactIntegersAndUsefulFractions) {
  const float world_per_map_unit = static_cast<float>(
      1.0 / quader::viewport_host_defaults().map_units_per_world_unit);
  present(hover_edge_frame({}, {64.0f * world_per_map_unit, 0.0f, 0.0f}));
  EXPECT_EQ(label_->text(), QStringLiteral("64"));

  present(hover_edge_frame({}, {world_per_map_unit, world_per_map_unit, 0.0f}));
  EXPECT_EQ(label_->text(), QStringLiteral("1.41"));

  present(hover_edge_frame({}, {0.0625f * world_per_map_unit, 0.0f, 0.0f}));
  EXPECT_EQ(label_->text(), QStringLiteral("0.0625"));
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
       ProjectsTheActualMidpointIntoLogicalPixelsAndUpdatesOnResize) {
  const quader::ViewportOverlayFrame frame =
      hover_edge_frame({1.0f, 3.0f, 0.0f}, {3.0f, 3.0f, 0.0f});
  const std::uint64_t signature =
      quader::RenderSignatureBuilder::overlay_frame_signature(frame);
  const quader::ViewportScreenPoint projected =
      camera_.project_world_point({2.0f, 3.0f, 0.0f});
  ASSERT_TRUE(projected.visible);
  const QRect logical_bounds{10, 20, 320, 240};
  hud_->present(frame, signature, camera_, logical_bounds, true);

  ASSERT_FALSE(label_->isHidden());
  EXPECT_NEAR(label_->geometry().center().x(),
              logical_bounds.x() + projected.position.x * 320.0f / 800.0f,
              1.0f);
  EXPECT_NEAR(label_->geometry().center().y(),
              logical_bounds.y() + projected.position.y * 240.0f / 600.0f,
              1.0f);

  const QRect resized_bounds{0, 0, 400, 300};
  hud_->present(frame, signature, camera_, resized_bounds, true);
  EXPECT_NEAR(label_->geometry().center().x(), projected.position.x * 0.5f,
              1.0f);
  EXPECT_NEAR(label_->geometry().center().y(), projected.position.y * 0.5f,
              1.0f);

  camera_.unlock_orthographic_view();
  hud_->present(frame, signature, camera_, resized_bounds, true);
  const quader::ViewportScreenPoint perspective =
      camera_.project_world_point({2.0f, 3.0f, 0.0f});
  ASSERT_FALSE(label_->isHidden());
  EXPECT_NEAR(label_->geometry().center().x(), perspective.position.x * 0.5f,
              1.0f);
  EXPECT_NEAR(label_->geometry().center().y(), perspective.position.y * 0.5f,
              1.0f);
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
      hover_edge_frame({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
  present(frame);
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts initial = events.counts();
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
  EXPECT_EQ(label_->text(), original_text);

  hud_->hide();
  QCoreApplication::processEvents();
  const EdgeHudWidgetEventCounter::Counts hidden = events.counts();
  EXPECT_GT(hidden.hide, moved.hide);
  for (int repetition = 0; repetition < 64; ++repetition) {
    present(frame, false);
  }
  QCoreApplication::processEvents();
  EXPECT_EQ(events.counts(), hidden);
  present(frame);
  EXPECT_FALSE(label_->isHidden());
}

} // namespace
