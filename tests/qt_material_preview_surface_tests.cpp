////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_preview_surface.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QMouseEvent>
#include <QWheelEvent>

#include <utility>

namespace quader {
namespace {

TEST(QtMaterialPreviewSurfaceTests,
     ExposesNeutralStateWithoutOwningProjectOrWorkspaceState) {
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-material-preview-surface-tests";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  QApplication::setStyle(QStringLiteral("Fusion"));
  application.setQuitOnLastWindowClosed(false);

  QtMaterialPreviewSurface surface;
  int changed_count = 0;
  surface.set_surface_changed_callback([&changed_count]() {
    ++changed_count;
  });

  EXPECT_FALSE(surface.ready_to_render());
  EXPECT_FALSE(surface.render_frame());
  EXPECT_EQ(surface.preview_state().projection(),
            MaterialPreviewProjection::Perspective);
  EXPECT_TRUE(surface.set_preview_mesh(MaterialPreviewMeshKind::RoundedBox));
  EXPECT_FALSE(surface.set_preview_mesh(MaterialPreviewMeshKind::RoundedBox));
  EXPECT_TRUE(surface.set_preview_shading(
      MaterialPreviewShadingMode::Wireframe));
  EXPECT_FALSE(surface.set_preview_shading(
      MaterialPreviewShadingMode::Wireframe));
  EXPECT_EQ(surface.preview_state().mesh_kind(),
            MaterialPreviewMeshKind::RoundedBox);
  EXPECT_EQ(surface.preview_state().shading_mode(),
            MaterialPreviewShadingMode::Wireframe);
  EXPECT_EQ(changed_count, 2);

  StandardMaterialPreviewSnapshot material;
  material.content_revision = 3u;
  ASSERT_NE(material.standard_material(), nullptr);
  material.standard_material()->base_color = {0.2f, 0.3f, 0.4f};
  surface.set_material(std::move(material));
  EXPECT_EQ(changed_count, 3);

  const float original_yaw = surface.preview_state().yaw_radians();
  QMouseEvent right_press(
      QEvent::MouseButtonPress, QPointF(10.0, 10.0), QPointF(10.0, 10.0),
      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &right_press);
  QMouseEvent right_move(
      QEvent::MouseMove, QPointF(40.0, 25.0), QPointF(40.0, 25.0),
      Qt::NoButton, Qt::RightButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &right_move);
  EXPECT_FLOAT_EQ(surface.preview_state().yaw_radians(), original_yaw);

  QMouseEvent left_press(
      QEvent::MouseButtonPress, QPointF(10.0, 10.0), QPointF(10.0, 10.0),
      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &left_press);
  QMouseEvent left_move(
      QEvent::MouseMove, QPointF(50.0, 30.0), QPointF(50.0, 30.0),
      Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &left_move);
  EXPECT_NE(surface.preview_state().yaw_radians(), original_yaw);
  QMouseEvent left_release(
      QEvent::MouseButtonRelease, QPointF(50.0, 30.0),
      QPointF(50.0, 30.0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &left_release);

  const float distance_before_wheel =
      surface.preview_state().camera_distance();
  QWheelEvent wheel(
      QPointF(30.0, 30.0), QPointF(30.0, 30.0), QPoint{},
      QPoint{0, 120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
      false);
  QCoreApplication::sendEvent(&surface, &wheel);
  EXPECT_NE(surface.preview_state().camera_distance(), distance_before_wheel);

  QMouseEvent double_click(
      QEvent::MouseButtonDblClick, QPointF(30.0, 30.0),
      QPointF(30.0, 30.0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &double_click);
  EXPECT_FLOAT_EQ(surface.preview_state().yaw_radians(), 0.0f);
  EXPECT_FLOAT_EQ(surface.preview_state().pitch_radians(), 0.0f);
  EXPECT_FLOAT_EQ(surface.preview_state().camera_distance(),
                  MaterialPreviewState::kDefaultCameraDistance);
}

} // namespace
} // namespace quader
