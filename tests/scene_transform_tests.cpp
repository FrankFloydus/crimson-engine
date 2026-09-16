////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace {

constexpr float kEpsilon = 0.0001F;

quader::QVec3 radians(quader::QVec3 degrees) {
  constexpr float kRadiansPerDegree = 0.01745329251994329577F;
  return degrees * kRadiansPerDegree;
}

void expect_vector_near(quader::QVec3 actual, quader::QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, kEpsilon);
  EXPECT_NEAR(actual.y, expected.y, kEpsilon);
  EXPECT_NEAR(actual.z, expected.z, kEpsilon);
}

void expect_matrix_near(const quader::QMat4 &actual,
                        const quader::QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], kEpsilon)
        << "Matrix element " << index;
  }
}

void expect_orthonormal_right_handed(const glm::mat3 &basis) {
  for (glm::length_t column = 0; column < 3; ++column) {
    EXPECT_NEAR(glm::length(basis[column]), 1.0F, kEpsilon);
  }
  EXPECT_NEAR(glm::dot(basis[0], basis[1]), 0.0F, kEpsilon);
  EXPECT_NEAR(glm::dot(basis[0], basis[2]), 0.0F, kEpsilon);
  EXPECT_NEAR(glm::dot(basis[1], basis[2]), 0.0F, kEpsilon);
  EXPECT_NEAR(glm::determinant(basis), 1.0F, kEpsilon);
}

TEST(SceneTransformTests,
     PureWorldRotationRemovesPositiveNonuniformScale) {
  const quader::QVec3 euler{0.37F, -0.61F, 1.12F};
  const quader::QTransform transform{
      .position = {4.0F, -2.0F, 9.0F},
      .rotation = quader::quat_from_euler_radians(euler),
      .scale = {2.0F, 0.25F, 7.0F},
  };

  const std::optional<glm::mat3> actual =
      quader::try_scene_pure_world_rotation_basis(
          quader::compose_transform(transform));

  ASSERT_TRUE(actual.has_value());
  const glm::mat3 expected =
      glm::mat3_cast(quader::to_glm(transform.rotation));
  for (glm::length_t column = 0; column < 3; ++column) {
    for (glm::length_t row = 0; row < 3; ++row) {
      EXPECT_NEAR((*actual)[column][row], expected[column][row], kEpsilon);
    }
  }
  expect_orthonormal_right_handed(*actual);
}

TEST(SceneTransformTests,
     PureWorldRotationMakesMirroredNonuniformBasisRightHanded) {
  const quader::QTransform transform{
      .rotation =
          quader::quat_from_euler_radians({-0.43F, 0.89F, 0.21F}),
      .scale = {-0.125F, 3.0F, 11.0F},
  };

  const std::optional<glm::mat3> actual =
      quader::try_scene_pure_world_rotation_basis(
          quader::compose_transform(transform));

  ASSERT_TRUE(actual.has_value());
  expect_orthonormal_right_handed(*actual);
}

TEST(SceneTransformTests, PureWorldRotationRejectsInvalidOrSingularMatrices) {
  quader::QTransform singular;
  singular.scale = {1.0F, 0.0F, 1.0F};
  EXPECT_FALSE(quader::try_scene_pure_world_rotation_basis(
                   quader::compose_transform(singular))
                   .has_value());

  quader::QMat4 nonfinite;
  nonfinite.values[5] = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(
      quader::try_scene_pure_world_rotation_basis(nonfinite).has_value());
}

TEST(SceneTransformTests, MatrixRoundTripPreservesQuarterTurnAndMixedRotations) {
  using namespace quader;
  const QVec3 authored_degrees[] = {
      {89.999F, 0.0F, 0.0F},  {90.0F, 0.0F, 0.0F},
      {90.001F, 0.0F, 0.0F},  {-90.0F, 0.0F, 0.0F},
      {0.0F, 89.999F, 0.0F},  {0.0F, 90.0F, 0.0F},
      {0.0F, 90.001F, 0.0F},  {0.0F, -90.0F, 0.0F},
      {0.0F, 0.0F, 89.999F},  {0.0F, 0.0F, 90.0F},
      {0.0F, 0.0F, 90.001F},  {0.0F, 0.0F, -90.0F},
      {20.0F, 89.999F, 50.0F}, {20.0F, 90.0F, 50.0F},
      {20.0F, 90.001F, 50.0F}, {20.0F, -90.0F, 50.0F},
      {83.129654F, 89.999985F, 147.11522F},
      {48.972073F, 90.00001F, -9.192518F},
  };
  for (const QVec3 degrees : authored_degrees) {
    SCOPED_TRACE(::testing::Message()
                 << degrees.x << ", " << degrees.y << ", " << degrees.z);
    const TransformComponent authored{
        .position = {4.0F, -2.0F, 9.0F},
        .rotation_euler = radians(degrees),
        .scale = {2.0F, 0.25F, 7.0F},
    };
    const QMat4 matrix = compose_transform(authored.spatial_transform());
    for (const QVec3 reference : {QVec3{}, authored.rotation_euler}) {
      const auto restored =
          try_transform_component_from_matrix(matrix, reference);
      ASSERT_TRUE(restored.has_value());
      expect_matrix_near(compose_transform(restored->spatial_transform()),
                         matrix);
    }
  }
}

TEST(SceneTransformTests,
     WorldRotationDeltaPreservesOrientationNearEulerSingularities) {
  using namespace quader;
  for (const QVec3 degrees :
       {QVec3{83.129654F, 89.999985F, 147.11522F},
        QVec3{48.972073F, 90.00001F, -9.192518F}}) {
    SCOPED_TRACE(::testing::Message()
                 << degrees.x << ", " << degrees.y << ", " << degrees.z);
    const TransformComponent before{.rotation_euler = radians(degrees)};
    for (const QVec3 delta : {QVec3{}, radians({0.0001F, 0.0F, 0.0F})}) {
      TransformComponent after = before;
      apply_world_rotation_delta_around_pivot(after, delta, {}, {});
      const QMat4 expected = scene_transform_multiply(
          compose_transform(QTransform{.rotation = quat_from_euler_radians(delta)}),
          compose_transform(before.spatial_transform()));
      expect_matrix_near(compose_transform(after.spatial_transform()), expected);
    }
  }
}

class MeshObjectTransformTests : public ::testing::Test {
protected:
  MeshObjectTransformTests()
      : editor_(quader::QuaderApiSessionInternalAccess::from_scene_session(
            session_)) {}

  void SetUp() override {
    using namespace quader;
    const auto created = editor_.modeling().create_box(
        QuaderBounds{.min = {-1.0F, -2.0F, -0.5F},
                     .max = {3.0F, 4.0F, 1.5F}},
        "Authored Asymmetric Mesh");
    ASSERT_TRUE(created.ok()) << created.error_message();
    object_ = created.value();
    ASSERT_TRUE(editor_.selection().select_object(object_).ok());
    const MeshAsset *mesh = session_.assets().mesh(object_.mesh);
    ASSERT_NE(mesh, nullptr);
    for (const MeshVertex &vertex : mesh->geometry().vertices) {
      authored_vertices_.push_back(vertex.position);
    }
    ASSERT_FALSE(authored_vertices_.empty());
  }

  void expect_authored_vertices_unchanged() const {
    const quader::MeshAsset *mesh = session_.assets().mesh(object_.mesh);
    ASSERT_NE(mesh, nullptr);
    ASSERT_EQ(mesh->geometry().vertices.size(), authored_vertices_.size());
    for (std::size_t index = 0; index < authored_vertices_.size(); ++index) {
      const auto actual = mesh->geometry().vertices[index].position;
      EXPECT_FLOAT_EQ(actual.x, authored_vertices_[index].x);
      EXPECT_FLOAT_EQ(actual.y, authored_vertices_[index].y);
      EXPECT_FLOAT_EQ(actual.z, authored_vertices_[index].z);
    }
  }

  void expect_authored_rotation(quader::QVec3 expected) const {
    using namespace quader;
    const TransformComponent *transform = session_.scene().transform(object_.entity);
    ASSERT_NE(transform, nullptr);
    expect_vector_near(transform->rotation_euler, expected);
    const QMat4 actual = compose_transform(
        QTransform{.rotation = quat_from_euler_radians(transform->rotation_euler)});
    const QMat4 requested = compose_transform(
        QTransform{.rotation = quat_from_euler_radians(expected)});
    expect_matrix_near(actual, requested);
    expect_authored_vertices_unchanged();
  }

  quader::QVec3 visible_pivot() const {
    const auto bounds = quader::selected_world_bounds(session_.scene(),
                                                     session_.assets());
    EXPECT_TRUE(bounds.has_value());
    return bounds ? quader::bounds_center(*bounds) : quader::QVec3{};
  }

  quader::SceneSession session_;
  quader::QuaderEditorApi editor_;
  quader::QuaderModelingObjectRef object_;
  std::vector<quader::QVec3> authored_vertices_;
};

TEST_F(MeshObjectTransformTests,
       InspectorOneShotRotationAssignsMixedAxesAndResetsToAuthoredZero) {
  using namespace quader;
  SceneTransformDragService service;
  const QVec3 requested_degrees[] = {
      {35.0F, 55.0F, -25.0F}, {0.0F, 55.0F, -25.0F},
      {0.0F, 0.0F, -25.0F},   {0.0F, 0.0F, 0.0F},
      {20.0F, 90.0F, 50.0F},  {0.0F, 0.0F, 0.0F},
  };
  for (const QVec3 degrees : requested_degrees) {
    SCOPED_TRACE(::testing::Message()
                 << degrees.x << ", " << degrees.y << ", " << degrees.z);
    const QVec3 pivot = visible_pivot();
    const auto result =
        service.rotate_selected_to_primary_euler_around_selection_pivot(
            editor_, session_.scene(), session_.assets(), object_.entity,
            radians(degrees), "Set Rotation");
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.changed());
    expect_authored_rotation(radians(degrees));
    expect_vector_near(visible_pivot(), pivot);
  }
}

TEST_F(MeshObjectTransformTests,
       InspectorPreviewKeepsRequestedRotationAcrossUpdatesCommitAndCancel) {
  using namespace quader;
  const QVec3 initial = radians({35.0F, 55.0F, -25.0F});
  ASSERT_TRUE(editor_.scene()
                  .set_transform(object_.entity, {.rotation_euler = initial})
                  .ok());
  SceneTransformDragService service;
  const QVec3 requested_degrees[] = {
      {0.0F, 55.0F, -25.0F}, {0.0F, 0.0F, -25.0F},
      {0.0F, 0.0F, 0.0F},   {20.0F, 90.0F, 50.0F},
  };
  for (const QVec3 degrees : requested_degrees) {
    SCOPED_TRACE(::testing::Message()
                 << degrees.x << ", " << degrees.y << ", " << degrees.z);
    const QVec3 pivot = visible_pivot();
    ASSERT_TRUE(service.begin_rotate_selected_to_primary_euler_preview(
        session_.scene(), session_.assets(), object_.entity, "Set Rotation"));
    const auto intermediate = service.preview_selected_to_primary_euler(
        session_.scene(), object_.entity, radians({15.0F, 35.0F, 5.0F}));
    ASSERT_TRUE(intermediate.ok) << intermediate.message;
    expect_authored_rotation(radians({15.0F, 35.0F, 5.0F}));
    const auto preview = service.preview_selected_to_primary_euler(
        session_.scene(), object_.entity, radians(degrees));
    ASSERT_TRUE(preview.ok) << preview.message;
    expect_authored_rotation(radians(degrees));
    expect_vector_near(visible_pivot(), pivot);
    const auto commit = service.commit(editor_);
    ASSERT_TRUE(commit.ok()) << commit.error_message();
    expect_authored_rotation(radians(degrees));
  }

  ASSERT_TRUE(service.begin_rotate_selected_to_primary_euler_preview(
      session_.scene(), session_.assets(), object_.entity, "Set Rotation"));
  ASSERT_TRUE(service.preview_selected_to_primary_euler(
      session_.scene(), object_.entity, {}).ok);
  expect_authored_rotation({});
  ASSERT_TRUE(service.cancel().ok);
  expect_authored_rotation(radians({20.0F, 90.0F, 50.0F}));
}

TEST_F(MeshObjectTransformTests,
       InspectorMixedSelectionRotatesPeersAroundSharedPivotAndReplaysHistory) {
  using namespace quader;
  std::array<QuaderModelingObjectRef, 3> objects{object_};
  for (std::size_t index = 1; index < objects.size(); ++index) {
    const auto created = editor_.modeling().create_box(
        QuaderBounds{.min = {-1.0F, -2.0F, -0.5F},
                     .max = {3.0F, 4.0F, 1.5F}},
        "Rotation Peer");
    ASSERT_TRUE(created.ok()) << created.error_message();
    objects[index] = created.value();
  }
  const std::array<QVec3, 3> initial_rotations{
      radians({35.0F, 55.0F, -25.0F}),
      radians({-15.0F, 20.0F, 65.0F}),
      radians({70.0F, -30.0F, 5.0F}),
  };
  const std::array<QVec3, 3> positions{
      QVec3{4.0F, -3.0F, 2.0F},
      QVec3{-6.0F, 2.0F, 1.0F},
      QVec3{1.0F, 5.0F, -4.0F},
  };
  std::array<QMat4, 3> before;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    ASSERT_TRUE(editor_.scene()
                    .set_transform(objects[index].entity,
                                   {.position = positions[index],
                                    .rotation_euler = initial_rotations[index]})
                    .ok());
    before[index] = session_.scene().world_transform_matrix(objects[index].entity);
  }
  ASSERT_TRUE(editor_.scene()
                  .select({objects[0].entity, objects[1].entity, objects[2].entity},
                          object_.entity)
                  .ok());
  const QVec3 pivot = visible_pivot();
  const QVec3 requested = radians({-40.0F, 70.0F, 15.0F});
  const glm::quat delta =
      to_glm(quat_from_euler_radians(requested)) *
      glm::inverse(to_glm(quat_from_euler_radians(initial_rotations[0])));
  const QMat4 group_delta = from_glm(
      glm::translate(glm::mat4{1.0F}, to_glm(pivot)) *
      glm::mat4_cast(delta) *
      glm::translate(glm::mat4{1.0F}, -to_glm(pivot)));
  std::array<QMat4, 3> expected;
  for (std::size_t index = 0; index < objects.size(); ++index) {
    expected[index] = scene_transform_multiply(group_delta, before[index]);
  }
  const auto expect_selection = [&](const std::array<QMat4, 3> &matrices) {
    for (std::size_t index = 0; index < objects.size(); ++index) {
      SCOPED_TRACE(index);
      expect_matrix_near(
          session_.scene().world_transform_matrix(objects[index].entity),
          matrices[index]);
      const MeshAsset *mesh = session_.assets().mesh(objects[index].mesh);
      ASSERT_NE(mesh, nullptr);
      ASSERT_EQ(mesh->geometry().vertices.size(), authored_vertices_.size());
      for (std::size_t vertex = 0; vertex < authored_vertices_.size(); ++vertex) {
        const QVec3 actual = mesh->geometry().vertices[vertex].position;
        EXPECT_FLOAT_EQ(actual.x, authored_vertices_[vertex].x);
        EXPECT_FLOAT_EQ(actual.y, authored_vertices_[vertex].y);
        EXPECT_FLOAT_EQ(actual.z, authored_vertices_[vertex].z);
      }
    }
  };

  SceneTransformDragService service;
  ASSERT_TRUE(service.begin_rotate_selected_to_primary_euler_preview(
      session_.scene(), session_.assets(), object_.entity, "Rotate Selection"));
  const auto preview = service.preview_selected_to_primary_euler(
      session_.scene(), object_.entity, requested);
  ASSERT_TRUE(preview.ok) << preview.message;
  expect_authored_rotation(requested);
  expect_selection(expected);
  const std::size_t undo_count = session_.history().undo_count();
  ASSERT_TRUE(service.commit(editor_).ok());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1);
  expect_selection(expected);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_authored_rotation(initial_rotations[0]);
  expect_selection(before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_authored_rotation(requested);
  expect_selection(expected);
}

TEST_F(MeshObjectTransformTests,
       WorldGizmoRotationThenInspectorZeroRestoresLocalOrientationWithHistory) {
  using namespace quader;
  ASSERT_TRUE(editor_.scene()
                  .set_transform(object_.entity,
                                 {.position = {4.0F, -3.0F, 2.0F},
                                  .rotation_euler = radians({20.0F, 35.0F, 50.0F}),
                                  .scale = {1.25F, 0.5F, 2.0F}})
                  .ok());
  const QMat4 before = session_.scene().world_transform_matrix(object_.entity);
  const QVec3 pivot = visible_pivot();
  const QMat4 world_delta = from_glm(
      glm::translate(glm::mat4{1.0F}, to_glm(pivot)) *
      glm::mat4_cast(to_glm(quat_from_euler_radians(radians({0.0F, 90.0F, 0.0F})))) *
      glm::translate(glm::mat4{1.0F}, -to_glm(pivot)));
  SceneTransformDragService service;
  ASSERT_TRUE(service.begin(session_.scene(), pivot, true, "Gizmo Rotation",
                            {}, &session_.assets()));
  const auto preview = service.preview_world_transform_delta(
      session_.scene(), world_delta, SceneTextureTransformKind::Rotation);
  ASSERT_TRUE(preview.ok) << preview.message;
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity),
                     scene_transform_multiply(world_delta, before));
  expect_authored_vertices_unchanged();
  ASSERT_TRUE(service.commit(editor_).ok());
  const QMat4 gizmo_rotated = session_.scene().world_transform_matrix(object_.entity);
  const auto reset =
      service.rotate_selected_to_primary_euler_around_selection_pivot(
          editor_, session_.scene(), session_.assets(), object_.entity, {},
          "Reset Rotation");
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  expect_authored_rotation({});
  expect_vector_near(visible_pivot(), pivot);
  const QMat4 reset_world = session_.scene().world_transform_matrix(object_.entity);

  ASSERT_TRUE(editor_.history().undo().ok());
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), gizmo_rotated);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), gizmo_rotated);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), reset_world);
  expect_authored_rotation({});
}

TEST_F(MeshObjectTransformTests,
       ParentedInspectorRotationUsesAuthoredLocalAxesAndPinsVisiblePivot) {
  using namespace quader;
  const auto parent = editor_.scene().create_empty();
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(editor_.scene()
                  .set_transform(parent.value().id,
                                 {.position = {10.0F, -5.0F, 3.0F},
                                  .rotation_euler = radians({25.0F, -40.0F, 15.0F}),
                                  .scale = {2.0F, 2.0F, 2.0F}})
                  .ok());
  ASSERT_TRUE(editor_.scene().reparent(object_.entity, parent.value().id, false).ok());
  ASSERT_TRUE(editor_.scene()
                  .set_transform(object_.entity,
                                 {.position = {2.0F, -1.0F, 4.0F},
                                  .rotation_euler = radians({35.0F, 55.0F, -25.0F})})
                  .ok());
  ASSERT_TRUE(editor_.selection().select_object(object_).ok());
  const QVec3 pivot = visible_pivot();
  SceneTransformDragService service;
  ASSERT_TRUE(service.begin_rotate_selected_to_primary_euler_preview(
      session_.scene(), session_.assets(), object_.entity, "Set Rotation"));
  const QVec3 requested = radians({15.0F, 90.0F, -40.0F});
  const auto preview = service.preview_selected_to_primary_euler(
      session_.scene(), object_.entity, requested);
  ASSERT_TRUE(preview.ok) << preview.message;
  expect_authored_rotation(requested);
  expect_vector_near(visible_pivot(), pivot);
  ASSERT_TRUE(service.commit(editor_).ok());
  expect_authored_rotation(requested);

  const auto reset =
      service.rotate_selected_to_primary_euler_around_selection_pivot(
          editor_, session_.scene(), session_.assets(), object_.entity, {},
          "Reset Rotation");
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  expect_authored_rotation({});
  expect_vector_near(visible_pivot(), pivot);
}

TEST_F(MeshObjectTransformTests,
       LocalGizmoTinyRotationDoesNotFlipMeshNearEulerSingularities) {
  using namespace quader;
  for (const QVec3 degrees :
       {QVec3{83.129654F, 89.999985F, 147.11522F},
        QVec3{48.972073F, 90.00001F, -9.192518F}}) {
    ASSERT_TRUE(editor_.scene()
                    .set_transform(object_.entity, {.rotation_euler = radians(degrees)})
                    .ok());
    const QMat4 before = session_.scene().world_transform_matrix(object_.entity);
    const QVec3 pivot = visible_pivot();
    SceneTransformDragService service;
    ASSERT_TRUE(service.begin(session_.scene(), pivot, true, "Gizmo Rotation",
                              {}, &session_.assets()));
    const QVec3 delta = radians({0.0001F, 0.0F, 0.0F});
    const auto preview = service.preview_local_rotation_delta(session_.scene(), delta);
    ASSERT_TRUE(preview.ok) << preview.message;
    const glm::mat3 expected =
        glm::mat3{to_glm(before)} *
        glm::mat3_cast(to_glm(quat_from_euler_radians(delta)));
    const glm::mat3 actual{
        to_glm(session_.scene().world_transform_matrix(object_.entity))};
    for (glm::length_t column = 0; column < 3; ++column) {
      for (glm::length_t row = 0; row < 3; ++row) {
        EXPECT_NEAR(actual[column][row], expected[column][row], kEpsilon);
      }
    }
    expect_vector_near(visible_pivot(), pivot);
    expect_authored_vertices_unchanged();
    ASSERT_TRUE(service.cancel().ok);
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), before);
  }
}

TEST_F(MeshObjectTransformTests,
       GlobalGizmoPreservesSignedScaleAndZeroRestoresAuthoredBasis) {
  using namespace quader;
  for (const QVec3 scale :
       {QVec3{-1.0F, 2.0F, 3.0F}, QVec3{-1.0F, -2.0F, 3.0F}}) {
    SCOPED_TRACE(::testing::Message()
                 << scale.x << ", " << scale.y << ", " << scale.z);
    ASSERT_TRUE(editor_.scene()
                    .set_transform(object_.entity,
                                   {.position = {4.0F, -3.0F, 2.0F},
                                    .rotation_euler = {},
                                    .scale = scale})
                    .ok());
    const QMat4 before = session_.scene().world_transform_matrix(object_.entity);
    const QVec3 pivot = visible_pivot();
    const QMat4 world_delta = from_glm(
        glm::translate(glm::mat4{1.0F}, to_glm(pivot)) *
        glm::mat4_cast(to_glm(
            quat_from_euler_radians(radians({0.0F, 0.0F, 45.0F})))) *
        glm::translate(glm::mat4{1.0F}, -to_glm(pivot)));
    const QMat4 rotated = scene_transform_multiply(world_delta, before);
    SceneTransformDragService service;
    ASSERT_TRUE(service.begin(session_.scene(), pivot, true, "Gizmo Rotation",
                              {}, &session_.assets()));
    const auto preview = service.preview_world_transform_delta(
        session_.scene(), world_delta, SceneTextureTransformKind::Rotation);
    ASSERT_TRUE(preview.ok) << preview.message;
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), rotated);
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);
    expect_authored_vertices_unchanged();
    ASSERT_TRUE(service.commit(editor_).ok());
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);

    const QVec3 rotated_euler =
        session_.scene().transform(object_.entity)->rotation_euler;
    const std::size_t undo_count = session_.history().undo_count();
    ASSERT_TRUE(service.begin(session_.scene(), visible_pivot(), true,
                              "Gizmo Translation", {}, &session_.assets()));
    const QVec3 translation{2.0F, -3.0F, 1.0F};
    const auto translation_preview = service.preview_world_translation_delta(
        session_.scene(), translation);
    ASSERT_TRUE(translation_preview.ok) << translation_preview.message;
    expect_authored_rotation(rotated_euler);
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);
    expect_matrix_near(
        session_.scene().world_transform_matrix(object_.entity),
        scene_transform_multiply(
            compose_transform(QTransform{.position = translation}), rotated));
    ASSERT_TRUE(service.cancel().ok);
    EXPECT_EQ(session_.history().undo_count(), undo_count);
    expect_authored_rotation(rotated_euler);
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), rotated);

    const auto reset =
        service.rotate_selected_to_primary_euler_around_selection_pivot(
            editor_, session_.scene(), session_.assets(), object_.entity, {},
            "Reset Rotation");
    ASSERT_TRUE(reset.ok()) << reset.error_message();
    expect_authored_rotation({});
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), before);

    ASSERT_TRUE(editor_.history().undo().ok());
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), rotated);
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);
    ASSERT_TRUE(editor_.history().undo().ok());
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), before);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), rotated);
    ASSERT_TRUE(editor_.history().redo().ok());
    expect_authored_rotation({});
    expect_vector_near(session_.scene().transform(object_.entity)->scale, scale);
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), before);
  }
}

TEST_F(MeshObjectTransformTests,
       GlobalGizmoRejectsShearUnderNonuniformParentWithoutMutation) {
  using namespace quader;
  const auto parent = editor_.scene().create_empty();
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(editor_.scene()
                  .set_transform(parent.value().id, {.scale = {2.0F, 1.0F, 1.0F}})
                  .ok());
  ASSERT_TRUE(editor_.scene().reparent(object_.entity, parent.value().id, false).ok());
  ASSERT_TRUE(editor_.selection().select_object(object_).ok());
  for (const float scale : {1.0F, 0.001F}) {
    ASSERT_TRUE(editor_.scene()
                    .set_transform(object_.entity, {.scale = {scale, scale, scale}})
                    .ok());
    for (const float degrees : {45.0F, 1.0F}) {
      SCOPED_TRACE(::testing::Message() << "Scale " << scale << ", angle " << degrees);
      const TransformComponent before = *session_.scene().transform(object_.entity);
      const std::size_t undo_count = session_.history().undo_count();
      const std::size_t redo_count = session_.history().redo_count();
      SceneTransformDragService service;
      ASSERT_TRUE(service.begin(session_.scene(), visible_pivot(), true,
                                "Gizmo Rotation", {}, &session_.assets()));
      const QMat4 world_delta = compose_transform(QTransform{
          .rotation = quat_from_euler_radians(radians({0.0F, 0.0F, degrees}))});
      const auto preview = service.preview_world_transform_delta(
          session_.scene(), world_delta, SceneTextureTransformKind::Rotation);
      EXPECT_FALSE(preview.ok);
      EXPECT_FALSE(preview.changed);
      const auto expect_unchanged = [&]() {
        const TransformComponent *actual = session_.scene().transform(object_.entity);
        ASSERT_NE(actual, nullptr);
        expect_vector_near(actual->position, before.position);
        expect_vector_near(actual->rotation_euler, before.rotation_euler);
        expect_vector_near(actual->scale, before.scale);
        expect_authored_vertices_unchanged();
        EXPECT_EQ(session_.history().undo_count(), undo_count);
        EXPECT_EQ(session_.history().redo_count(), redo_count);
      };
      expect_unchanged();
      ASSERT_TRUE(service.cancel().ok);
      expect_unchanged();
    }
  }
}

} // namespace
