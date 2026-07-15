////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_material_preview_surface.hpp"

#include "math/glm_bridge.hpp"

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
  EXPECT_TRUE(surface.set_preview_environment("pillars_2k"));
  EXPECT_FALSE(surface.set_preview_environment("pillars_2k"));
  EXPECT_FALSE(surface.set_preview_environment("missing_environment"));
  EXPECT_TRUE(surface.set_show_environment_background(true));
  EXPECT_FALSE(surface.set_show_environment_background(true));
  EXPECT_EQ(surface.preview_state().mesh_kind(),
            MaterialPreviewMeshKind::RoundedBox);
  EXPECT_EQ(surface.preview_state().shading_mode(),
            MaterialPreviewShadingMode::Wireframe);
  EXPECT_EQ(surface.preview_state().environment_id(), "pillars_2k");
  EXPECT_TRUE(surface.preview_state().show_environment_background());
  EXPECT_EQ(changed_count, 4);

  StandardMaterialPreviewSnapshot material;
  material.content_revision = 3u;
  ASSERT_NE(material.standard_material(), nullptr);
  material.standard_material()->base_color = {0.2f, 0.3f, 0.4f};
  surface.set_material(std::move(material));
  EXPECT_EQ(changed_count, 5);

  const float original_yaw = surface.preview_state().yaw_radians();
  const float original_pitch = surface.preview_state().pitch_radians();
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
  EXPECT_FLOAT_EQ(surface.preview_state().yaw_radians(), original_yaw);
  QMouseEvent left_release(
      QEvent::MouseButtonRelease, QPointF(50.0, 30.0),
      QPointF(50.0, 30.0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &left_release);

  QMouseEvent middle_press(
      QEvent::MouseButtonPress, QPointF(10.0, 30.0), QPointF(10.0, 30.0),
      Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &middle_press);
  QMouseEvent middle_move(
      QEvent::MouseMove, QPointF(50.0, 10.0), QPointF(50.0, 10.0),
      Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &middle_move);
  EXPECT_NE(surface.preview_state().yaw_radians(), original_yaw);
  EXPECT_LT(surface.preview_state().pitch_radians(), original_pitch);
  const QVec3 transformed_front = transform_vector(
      compose_transform(
          QTransform{.rotation = surface.preview_state().object_rotation()}),
      {0.0f, 0.0f, 1.0f});
  EXPECT_GT(transformed_front.y, 0.0f);
  QMouseEvent middle_release(
      QEvent::MouseButtonRelease, QPointF(50.0, 10.0),
      QPointF(50.0, 10.0), Qt::MiddleButton, Qt::NoButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &middle_release);

  const QVec3 up_before_horizontal_drag = transform_vector(
      compose_transform(
          QTransform{.rotation = surface.preview_state().object_rotation()}),
      {0.0f, 1.0f, 0.0f});
  const float pitch_before_horizontal_drag =
      surface.preview_state().pitch_radians();
  QMouseEvent horizontal_press(
      QEvent::MouseButtonPress, QPointF(20.0, 30.0), QPointF(20.0, 30.0),
      Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &horizontal_press);
  QMouseEvent horizontal_move(
      QEvent::MouseMove, QPointF(70.0, 30.0), QPointF(70.0, 30.0),
      Qt::NoButton, Qt::MiddleButton, Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &horizontal_move);
  QMouseEvent horizontal_release(
      QEvent::MouseButtonRelease, QPointF(70.0, 30.0),
      QPointF(70.0, 30.0), Qt::MiddleButton, Qt::NoButton,
      Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &horizontal_release);
  const QVec3 up_after_horizontal_drag = transform_vector(
      compose_transform(
          QTransform{.rotation = surface.preview_state().object_rotation()}),
      {0.0f, 1.0f, 0.0f});
  EXPECT_FLOAT_EQ(surface.preview_state().pitch_radians(),
                  pitch_before_horizontal_drag);
  EXPECT_NEAR(up_after_horizontal_drag.x, up_before_horizontal_drag.x,
              0.00001f);
  EXPECT_NEAR(up_after_horizontal_drag.y, up_before_horizontal_drag.y,
              0.00001f);
  EXPECT_NEAR(up_after_horizontal_drag.z, up_before_horizontal_drag.z,
              0.00001f);

  const QVec2 pan_before = surface.preview_state().pan_offset();
  QMouseEvent pan_press(
      QEvent::MouseButtonPress, QPointF(10.0, 10.0), QPointF(10.0, 10.0),
      Qt::MiddleButton, Qt::MiddleButton, Qt::ShiftModifier);
  QCoreApplication::sendEvent(&surface, &pan_press);
  QMouseEvent pan_move(
      QEvent::MouseMove, QPointF(35.0, 25.0), QPointF(35.0, 25.0),
      Qt::NoButton, Qt::MiddleButton, Qt::ShiftModifier);
  QCoreApplication::sendEvent(&surface, &pan_move);
  EXPECT_NE(surface.preview_state().pan_offset().x, pan_before.x);
  EXPECT_NE(surface.preview_state().pan_offset().y, pan_before.y);
  QMouseEvent pan_release(
      QEvent::MouseButtonRelease, QPointF(35.0, 25.0),
      QPointF(35.0, 25.0), Qt::MiddleButton, Qt::NoButton,
      Qt::ShiftModifier);
  QCoreApplication::sendEvent(&surface, &pan_release);

  const float environment_rotation_before =
      surface.preview_state().environment_rotation_radians();
  QMouseEvent environment_press(
      QEvent::MouseButtonPress, QPointF(10.0, 10.0), QPointF(10.0, 10.0),
      Qt::MiddleButton, Qt::MiddleButton, Qt::ControlModifier);
  QCoreApplication::sendEvent(&surface, &environment_press);
  QMouseEvent environment_move(
      QEvent::MouseMove, QPointF(45.0, 10.0), QPointF(45.0, 10.0),
      Qt::NoButton, Qt::MiddleButton, Qt::ControlModifier);
  QCoreApplication::sendEvent(&surface, &environment_move);
  EXPECT_NE(surface.preview_state().environment_rotation_radians(),
            environment_rotation_before);
  QMouseEvent environment_release(
      QEvent::MouseButtonRelease, QPointF(45.0, 10.0),
      QPointF(45.0, 10.0), Qt::MiddleButton, Qt::NoButton,
      Qt::ControlModifier);
  QCoreApplication::sendEvent(&surface, &environment_release);

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
      QPointF(30.0, 30.0), Qt::MiddleButton, Qt::MiddleButton,
      Qt::NoModifier);
  QCoreApplication::sendEvent(&surface, &double_click);
  EXPECT_FLOAT_EQ(surface.preview_state().yaw_radians(), 0.0f);
  EXPECT_FLOAT_EQ(surface.preview_state().pitch_radians(), 0.0f);
  EXPECT_FLOAT_EQ(surface.preview_state().camera_distance(),
                  MaterialPreviewState::kDefaultCameraDistance);
  EXPECT_FLOAT_EQ(surface.preview_state().pan_offset().x, 0.0f);
  EXPECT_FLOAT_EQ(surface.preview_state().pan_offset().y, 0.0f);
  EXPECT_FLOAT_EQ(surface.preview_state().environment_rotation_radians(),
                  0.0f);
}

} // namespace
} // namespace quader
