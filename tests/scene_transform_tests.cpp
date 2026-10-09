////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/document/scene_entity_state.hpp"
#include "scene/scene_transform.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>
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

TEST(SceneTransformTests, UnparentPreservesWorldTransformAndAppendsAuthoredRoot) {
  quader::SceneDocument scene;
  const auto group = scene.create_empty("Group");
  const auto child = scene.create_empty("Child");
  const auto sibling = scene.create_empty("Sibling");
  ASSERT_TRUE(scene.set_transform(group, {
      .location = {4.0f, 2.0f, -1.0f},
      .rotation_euler = {0.0f, 0.5f, 0.0f},
      .scale = {2.0f, 2.0f, 2.0f},
  }));
  ASSERT_TRUE(scene.set_transform(child, {.location = {3.0f, 0.0f, 1.0f}}));
  ASSERT_TRUE(scene.set_parent(child, group, false));
  const auto before = scene.world_transform_matrix(child);
  ASSERT_TRUE(scene.set_parent(child, quader::kInvalidEntityId, true));
  expect_matrix_near(scene.world_transform_matrix(child), before);
  EXPECT_EQ(scene.root_entities(),
            std::vector<quader::EntityId>({group, sibling, child}));
  EXPECT_TRUE(scene.children(group).empty());
}

TEST(SceneTransformTests, FailedReparentPreservesRootOrderAndLocalTransform) {
  quader::SceneDocument scene;
  const auto group = scene.create_empty("Singular Group");
  const auto first = scene.create_empty("First");
  const auto second = scene.create_empty("Second");
  ASSERT_TRUE(scene.set_transform(group, {.scale = {0.0f, 1.0f, 1.0f}}));
  ASSERT_TRUE(scene.set_sibling_ordinal(second, 0u));
  const auto roots = scene.root_entities();
  const auto before = scene.world_transform_matrix(first);
  const auto revision = scene.revision();
  EXPECT_FALSE(scene.set_parent(first, group, true));
  EXPECT_EQ(scene.parent(first), quader::kInvalidEntityId);
  EXPECT_EQ(scene.root_entities(), roots);
  expect_matrix_near(scene.world_transform_matrix(first), before);
  EXPECT_EQ(scene.revision(), revision);
}

TEST(SceneTransformTests,
     ParentingRetainsEditableChannelsAndExactAffinePlacementAcrossParents) {
  using namespace quader;
  SceneDocument scene;
  const EntityId first_parent = scene.create_empty("First parent");
  const EntityId second_parent = scene.create_empty("Second parent");
  const EntityId child = scene.create_entity("Mesh", EntityKind::Mesh);
  const EntityId grandchild = scene.create_empty("Grandchild");
  ASSERT_TRUE(scene.set_transform(first_parent, {
      .location = {3.0f, -2.0f, 1.0f},
      .rotation_euler = {0.7f, -0.3f, 0.2f},
      .scale = {-2.0f, 0.5f, 3.0f},
  }));
  ASSERT_TRUE(scene.set_transform(second_parent, {
      .location = {-1.0f, 4.0f, 2.0f},
      .rotation_euler = {-0.4f, 0.8f, 0.1f},
      .scale = {0.5f, 1.5f, -2.0f},
  }));
  TransformComponent authored{
      .location = {1.0f, 2.0f, -3.0f},
      .rotation_euler = {0.6f, 0.3f, -0.9f},
      .scale = {0.75f, 2.0f, 1.0f},
      .custom_pivot = true,
  };
  authored.parenting_offset.values[4] = 0.35f;
  ASSERT_TRUE(scene.set_transform(child, authored));
  ASSERT_TRUE(scene.set_parent(child, first_parent, false));
  ASSERT_TRUE(scene.set_parent(grandchild, child, false));
  ASSERT_TRUE(scene.set_transform(grandchild, {.location = {2.0f, 1.0f, -1.0f}}));
  const QMat4 child_world = scene.world_transform_matrix(child);
  const QMat4 grandchild_world = scene.world_transform_matrix(grandchild);

  for (const EntityId destination :
       {second_parent, kInvalidEntityId, first_parent}) {
    ASSERT_TRUE(scene.set_parent(child, destination, true));
    const auto *actual = scene.transform(child);
    ASSERT_NE(actual, nullptr);
    expect_vector_near(actual->location, authored.location);
    expect_vector_near(actual->rotation_euler, authored.rotation_euler);
    expect_vector_near(actual->scale, authored.scale);
    EXPECT_TRUE(actual->custom_pivot);
    expect_matrix_near(scene.world_transform_matrix(child), child_world);
    expect_matrix_near(scene.world_transform_matrix(grandchild), grandchild_world);
    const auto projection = scene.hierarchy_projection_snapshot();
    ASSERT_NE(projection.find(child), nullptr);
    expect_matrix_near(projection.find(child)->world_transform, child_world);
  }
}

TEST(SceneTransformTests, ReparentWithoutPreservationRetainsCompleteLocalMatrix) {
  using namespace quader;
  SceneDocument scene;
  const EntityId parent = scene.create_empty("Parent");
  const EntityId child = scene.create_empty("Child");
  ASSERT_TRUE(scene.set_transform(parent, {.location = {8.0f, -3.0f, 2.0f}}));
  TransformComponent authored{.location = {1.0f, 2.0f, 3.0f}};
  authored.parenting_offset.values[4] = 0.5f;
  authored.parenting_offset.values[12] = -4.0f;
  ASSERT_TRUE(scene.set_transform(child, authored));
  const QMat4 local = scene.local_transform_matrix(child);
  ASSERT_TRUE(scene.set_parent(child, parent, false));
  EXPECT_EQ(scene.transform(child)->parenting_offset.values,
            authored.parenting_offset.values);
  expect_matrix_near(scene.local_transform_matrix(child), local);
  expect_matrix_near(scene.world_transform_matrix(child),
                     scene_transform_multiply(scene.world_transform_matrix(parent),
                                              local));
}

TEST(SceneTransformTests, SingularChildAndOldParentRemainPreservable) {
  using namespace quader;
  SceneDocument scene;
  const EntityId parent = scene.create_empty("Collapsed parent");
  const EntityId destination = scene.create_empty("Destination");
  const EntityId child = scene.create_empty("Collapsed child");
  ASSERT_TRUE(scene.set_transform(parent, {.scale = {0.0f, 2.0f, 1.0f}}));
  ASSERT_TRUE(scene.set_transform(destination, {
      .location = {5.0f, -1.0f, 3.0f},
      .rotation_euler = {0.3f, -0.8f, 0.2f},
  }));
  ASSERT_TRUE(scene.set_transform(child, {
      .location = {3.0f, 1.0f, 2.0f}, .scale = {1.0f, 0.0f, -2.0f}}));
  ASSERT_TRUE(scene.set_parent(child, parent, false));
  const QMat4 world = scene.world_transform_matrix(child);
  ASSERT_TRUE(scene.set_parent(child, kInvalidEntityId, true));
  expect_matrix_near(scene.world_transform_matrix(child), world);
  ASSERT_TRUE(scene.set_parent(child, destination, true));
  expect_matrix_near(scene.world_transform_matrix(child), world);
  EXPECT_FLOAT_EQ(scene.transform(child)->scale.y, 0.0f);
  EXPECT_TRUE(scene_matrix_is_affine(scene.transform(child)->parenting_offset));
}

TEST(SceneTransformTests, CompensationOnlyEditsInvalidateAndRollbackProjection) {
  using namespace quader;
  SceneDocument scene;
  const EntityId child = scene.create_empty("Child");
  TransformComponent authored{.location = {1.0f, 2.0f, 3.0f}};
  authored.parenting_offset.values[4] = 0.4f;
  ASSERT_TRUE(scene.set_transform(child, authored));
  const auto captured = capture_scene_entity_state(scene, child);
  ASSERT_TRUE(captured.has_value());
  EXPECT_EQ(captured->transform.parenting_offset.values,
            authored.parenting_offset.values);
  const std::array participants{child};
  const auto checkpoint = scene.capture_modeling_projection_checkpoint(
      participants, SceneModelingProjectionDomain::Structure);
  const auto old_projection = scene.hierarchy_projection_snapshot();
  const auto old_revision = scene.transform_revision();
  TransformComponent changed = authored;
  changed.parenting_offset.values[12] = 7.0f;
  ASSERT_TRUE(scene.set_transform(child, changed));
  EXPECT_GT(scene.transform_revision(), old_revision);
  const auto changed_projection = scene.hierarchy_projection_snapshot();
  ASSERT_NE(changed_projection.find(child), nullptr);
  EXPECT_NE(changed_projection.find(child)->world_transform.values,
            old_projection.find(child)->world_transform.values);
  const auto changed_revision = scene.transform_revision();
  ASSERT_TRUE(scene.set_transform(child, changed));
  EXPECT_EQ(scene.transform_revision(), changed_revision);
  ASSERT_TRUE(scene.restore_modeling_projection_checkpoint(checkpoint));
  EXPECT_EQ(scene.transform(child)->parenting_offset.values,
            authored.parenting_offset.values);
  expect_matrix_near(scene.world_transform_matrix(child),
                     old_projection.find(child)->world_transform);
}

TEST(SceneTransformTests, InvalidCompensationIsRejectedBeforePublication) {
  using namespace quader;
  SceneDocument scene;
  const EntityId child = scene.create_empty("Child");
  for (const auto [index, value] :
       {std::pair{3u, 0.01f}, std::pair{15u, 0.5f},
        std::pair{5u, std::numeric_limits<float>::infinity()}}) {
    TransformComponent invalid;
    invalid.parenting_offset.values[index] = value;
    const auto revision = scene.revision();
    EXPECT_FALSE(scene.set_transform(child, invalid));
    EXPECT_EQ(scene.revision(), revision);
    SceneEntityState state;
    state.transform = invalid;
    EXPECT_FALSE(apply_scene_entity_common_state(scene, child, state, "Invalid"));
    EXPECT_EQ(scene.name(child)->name, "Child");
    EXPECT_EQ(scene.revision(), revision);
  }
}

TEST(SceneTransformTests, ChannelWorldSolveRetainsCompensationAndRejectsShear) {
  using namespace quader;
  const QMat4 parent = compose_transform(QTransform{
      .location = {2.0f, -3.0f, 1.0f},
      .rotation = quat_from_euler_radians({0.2f, 0.5f, -0.7f}),
      .scale = {2.0f, 0.5f, -1.0f},
  });
  TransformComponent original{.custom_pivot = true};
  original.parenting_offset.values[4] = 0.25f;
  original.parenting_offset.values[12] = 3.0f;
  TransformComponent desired = original;
  desired.location = {4.0f, -1.0f, 2.0f};
  desired.rotation_euler = {0.3f, 0.6f, 0.2f};
  desired.scale = {2.0f, 3.0f, 4.0f};
  const QMat4 desired_world = scene_transform_multiply(
      parent, scene_local_transform_matrix(desired));
  const auto solved = try_scene_local_transform_preserving_world(
      parent, desired_world, original);
  ASSERT_TRUE(solved.has_value());
  EXPECT_EQ(solved->parenting_offset.values, original.parenting_offset.values);
  EXPECT_TRUE(solved->custom_pivot);
  expect_vector_near(solved->location, desired.location);
  expect_matrix_near(scene_transform_multiply(parent,
                                              scene_local_transform_matrix(*solved)),
                     desired_world);

  QMat4 requested_channels = compose_transform(
      QTransform{.scale = {0.001f, 0.001f, 0.001f}});
  requested_channels.values[4] = 0.0004f;
  const QMat4 sheared_world = scene_transform_multiply(
      scene_compensated_parent_matrix(parent, original), requested_channels);
  EXPECT_FALSE(try_scene_local_transform_preserving_world(
                   parent, sheared_world, original)
                   .has_value());
}

TEST(SceneTransformTests,
     PureWorldRotationRemovesPositiveNonuniformScale) {
  const quader::QVec3 euler{0.37F, -0.61F, 1.12F};
  const quader::QTransform transform{
      .location = {4.0F, -2.0F, 9.0F},
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
        .location = {4.0F, -2.0F, 9.0F},
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
                                   {.location = positions[index],
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
                                 {.location = {4.0F, -3.0F, 2.0F},
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
                                 {.location = {10.0F, -5.0F, 3.0F},
                                  .rotation_euler = radians({25.0F, -40.0F, 15.0F}),
                                  .scale = {2.0F, 2.0F, 2.0F}})
                  .ok());
  ASSERT_TRUE(editor_.scene().reparent(object_.entity, parent.value().id, false).ok());
  ASSERT_TRUE(editor_.scene()
                  .set_transform(object_.entity,
                                 {.location = {2.0F, -1.0F, 4.0F},
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
                                   {.location = {4.0F, -3.0F, 2.0F},
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
            compose_transform(QTransform{.location = translation}), rotated));
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
        expect_vector_near(actual->location, before.location);
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

class SceneNudgeTests : public MeshObjectTransformTests,
                        public ::testing::WithParamInterface<bool> {
protected:
  void SetUp() override {
    MeshObjectTransformTests::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    using namespace quader;
    const auto light = editor_.scene().create_point_light();
    ASSERT_TRUE(light.ok()) << light.error_message();
    light_ = light.value().id;
    ASSERT_TRUE(editor_.scene()
                    .set_transform(light_, {.location = {-4.0F, 3.0F, 2.0F},
                                            .rotation_euler = radians({20.0F, -15.0F, 35.0F})})
                    .ok());
    ASSERT_TRUE(editor_.scene()
                    .set_transform(object_.entity,
                                   {.location = {3.0F, -2.0F, 1.0F},
                                    .rotation_euler = radians({-25.0F, 40.0F, 10.0F}),
                                    .scale = {1.5F, 0.75F, 2.0F}})
                    .ok());
    if (GetParam()) {
      ASSERT_TRUE(editor_.scene().select({object_.entity, light_}, object_.entity).ok());
    } else {
      ASSERT_TRUE(editor_.scene().select(light_).ok());
    }
    ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Scale).ok());
    mesh_before_ = session_.scene().world_transform_matrix(object_.entity);
    light_before_ = session_.scene().world_transform_matrix(light_);
  }

  void expect_world_delta(const quader::QMat4 &delta) const {
    using namespace quader;
    expect_matrix_near(session_.scene().world_transform_matrix(light_),
                       scene_transform_multiply(delta, light_before_));
    expect_matrix_near(session_.scene().world_transform_matrix(object_.entity),
                       GetParam() ? scene_transform_multiply(delta, mesh_before_)
                                  : mesh_before_);
    expect_authored_vertices_unchanged();
    EXPECT_EQ(editor_.tools().active_tool(), "scale");
  }

  quader::EntityId light_ = quader::kInvalidEntityId;
  quader::QMat4 mesh_before_;
  quader::QMat4 light_before_;
};

TEST_P(SceneNudgeTests, MoveAndRepeatEachRecordOneReversibleSceneStep) {
  using namespace quader;
  ASSERT_TRUE(editor_.modeling().can_nudge_selection());
  const QuaderResolvedNudgeRequest request{
      .tool = QuaderTransformTool::Move,
      .axis = QuaderTransformAxis::X,
      .move_delta = {2.0F, -0.5F, 1.0F},
  };
  const auto undo_count = session_.history().undo_count();
  const auto nudged = editor_.modeling().nudge_selection(request);
  ASSERT_TRUE(nudged.ok()) << nudged.error_message();
  EXPECT_TRUE(nudged.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
  const QMat4 once = compose_transform(QTransform{.location = request.move_delta});
  const QMat4 twice = compose_transform(QTransform{.location = request.move_delta * 2.0F});
  expect_world_delta(once);

  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 2U);
  expect_world_delta(twice);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
  expect_world_delta(once);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.history().undo_count(), undo_count);
  expect_world_delta(scene_transform_identity_matrix());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_world_delta(once);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_world_delta(twice);
}

TEST_P(SceneNudgeTests, GlobalRotationAndRepeatUseTheResolvedPivotWithSceneHistory) {
  using namespace quader;
  const QVec3 pivot{1.0F, 2.0F, -1.0F};
  const QuaderResolvedNudgeRequest request{
      .tool = QuaderTransformTool::Rotate,
      .axis = QuaderTransformAxis::Z,
      .pivot = pivot,
      .rotate_angle_degrees = 30.0,
  };
  const QMat4 once = from_glm(
      glm::translate(glm::mat4{1.0F}, to_glm(pivot)) *
      glm::mat4_cast(to_glm(quat_from_euler_radians(radians({0.0F, 0.0F, 30.0F})))) *
      glm::translate(glm::mat4{1.0F}, -to_glm(pivot)));
  const QMat4 twice = scene_transform_multiply(once, once);
  const auto undo_count = session_.history().undo_count();
  const auto nudged = editor_.modeling().nudge_selection(request);
  ASSERT_TRUE(nudged.ok()) << nudged.error_message();
  EXPECT_TRUE(nudged.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
  expect_world_delta(once);
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 2U);
  expect_world_delta(twice);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_world_delta(once);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_world_delta(scene_transform_identity_matrix());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_world_delta(once);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_world_delta(twice);
}

INSTANTIATE_TEST_SUITE_P(NativeAndMixed, SceneNudgeTests, ::testing::Bool(),
                        [](const ::testing::TestParamInfo<bool> &info) {
                          return info.param ? "MeshAndLight" : "LightOnly";
                        });

TEST_F(MeshObjectTransformTests, LocalNudgeRotatesEachObjectAroundItsOwnVisibleOrCustomPivot) {
  using namespace quader;
  const auto peer = editor_.modeling().create_box(
      QuaderBounds{.min = {-1.0F, -2.0F, -0.5F}, .max = {3.0F, 4.0F, 1.5F}},
      "Custom Pivot Peer");
  ASSERT_TRUE(peer.ok()) << peer.error_message();
  const std::array entities{object_.entity, peer.value().entity};
  const std::array<QuaderTransform, 2> transforms{{
      {.location = {4.0F, -3.0F, 2.0F},
       .rotation_euler = radians({20.0F, 35.0F, 50.0F}),
       .scale = {1.25F, 0.5F, 2.0F}},
      {.location = {-6.0F, 2.0F, 1.0F},
       .rotation_euler = radians({-30.0F, 15.0F, 65.0F}),
       .scale = {0.75F, 2.0F, 1.5F}},
  }};
  for (std::size_t index = 0; index < entities.size(); ++index) {
    ASSERT_TRUE(editor_.scene().set_transform(entities[index], transforms[index]).ok());
  }
  TransformComponent custom = *session_.scene().transform(entities[1]);
  custom.custom_pivot = true;
  ASSERT_TRUE(session_.scene().set_transform(entities[1], custom));
  ASSERT_TRUE(editor_.scene().select({entities[0], entities[1]}, entities[0]).ok());
  ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Move).ok());
  std::array<QMat4, 2> before;
  std::array<QMat4, 2> expected;
  std::array<QVec3, 2> pivots;
  for (std::size_t index = 0; index < entities.size(); ++index) {
    before[index] = session_.scene().world_transform_matrix(entities[index]);
    const auto bounds = entity_world_bounds(session_.scene(), session_.assets(), entities[index]);
    ASSERT_TRUE(bounds.has_value());
    pivots[index] = index == 0 ? bounds_center(*bounds)
                               : transform_point(before[index], {});
    const auto inverse = try_scene_matrix_inverse(before[index]);
    ASSERT_TRUE(inverse.has_value());
    const QVec3 local_pivot = transform_point(*inverse, pivots[index]);
    const glm::quat rotation =
        to_glm(quat_from_euler_radians(transforms[index].rotation_euler)) *
        to_glm(quat_from_euler_radians(radians({15.0F, 0.0F, 0.0F})));
    QTransform after{.rotation = from_glm(rotation), .scale = transforms[index].scale};
    after.location = pivots[index] - transform_point(compose_transform(after), local_pivot);
    expected[index] = compose_transform(after);
  }
  const auto undo_count = session_.history().undo_count();
  const auto nudged = editor_.modeling().nudge_selection({
      .tool = QuaderTransformTool::Rotate,
      .axis = QuaderTransformAxis::X,
      .space = QuaderTransformSpace::Local,
      .pivot = {100.0F, -80.0F, 30.0F},
      .rotate_angle_degrees = 15.0,
  });
  ASSERT_TRUE(nudged.ok()) << nudged.error_message();
  EXPECT_TRUE(nudged.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
  for (std::size_t index = 0; index < entities.size(); ++index) {
    expect_matrix_near(session_.scene().world_transform_matrix(entities[index]), expected[index]);
  }
  const auto bounds = entity_world_bounds(session_.scene(), session_.assets(), entities[0]);
  ASSERT_TRUE(bounds.has_value());
  expect_vector_near(bounds_center(*bounds), pivots[0]);
  expect_vector_near(transform_point(expected[1], {}), pivots[1]);
  EXPECT_TRUE(session_.scene().transform(entities[1])->custom_pivot);
  EXPECT_EQ(editor_.tools().active_tool(), "move");
  expect_authored_vertices_unchanged();
  ASSERT_TRUE(editor_.history().undo().ok());
  for (std::size_t index = 0; index < entities.size(); ++index) {
    expect_matrix_near(session_.scene().world_transform_matrix(entities[index]), before[index]);
  }
  ASSERT_TRUE(editor_.history().redo().ok());
  for (std::size_t index = 0; index < entities.size(); ++index) {
    expect_matrix_near(session_.scene().world_transform_matrix(entities[index]), expected[index]);
  }
}

TEST_F(MeshObjectTransformTests, LockedMixedNudgeRejectsAtomicallyAndRetainsPreviousRepeat) {
  using namespace quader;
  const auto light = editor_.scene().create_point_light();
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(editor_.scene().select({object_.entity, light.value().id}, object_.entity).ok());
  const QuaderResolvedNudgeRequest request{
      .axis = QuaderTransformAxis::X, .move_delta = {2.0F, 0.0F, 0.0F}};
  ASSERT_TRUE(editor_.modeling().nudge_selection(request).ok());
  const QMat4 mesh_before = session_.scene().world_transform_matrix(object_.entity);
  const QMat4 light_before = session_.scene().world_transform_matrix(light.value().id);
  ASSERT_TRUE(session_.scene().set_selectable(light.value().id, false));
  const auto before = test::capture_command_observable_state(editor_);
  EXPECT_FALSE(editor_.modeling().can_nudge_selection());
  for (const bool duplicate : {false, true}) {
    SCOPED_TRACE(duplicate);
    const auto rejected = editor_.modeling().nudge_selection({
        .axis = QuaderTransformAxis::Y,
        .move_delta = {0.0F, 3.0F, 0.0F},
        .duplicate_selected = duplicate});
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(test::capture_command_observable_state(editor_), before);
  }
  ASSERT_TRUE(session_.scene().set_selectable(light.value().id, true));
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  const QMat4 delta = compose_transform(QTransform{.location = request.move_delta});
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity),
                     scene_transform_multiply(delta, mesh_before));
  expect_matrix_near(session_.scene().world_transform_matrix(light.value().id),
                     scene_transform_multiply(delta, light_before));
  expect_authored_vertices_unchanged();
}

TEST_F(MeshObjectTransformTests, ZeroNudgePreservesRedoAndTheLastNonzeroRepeat) {
  using namespace quader;
  ASSERT_TRUE(editor_.modeling().nudge_selection({
      .axis = QuaderTransformAxis::X, .move_delta = {1.0F, 0.0F, 0.0F}}).ok());
  ASSERT_TRUE(editor_.modeling().nudge_selection({
      .axis = QuaderTransformAxis::Y, .move_delta = {0.0F, 2.0F, 0.0F}}).ok());
  const QMat4 twice = session_.scene().world_transform_matrix(object_.entity);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().can_redo());
  const auto undo_count = session_.history().undo_count();
  const auto before = test::capture_command_observable_state(editor_);
  const auto unchanged = editor_.modeling().nudge_selection({.axis = QuaderTransformAxis::X});
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(test::capture_command_observable_state(editor_), before);
  const auto repeated = editor_.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_TRUE(repeated.changed());
  EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), twice);
}

TEST_F(MeshObjectTransformTests, NudgeRejectsPendingGeometryWithoutSettlingOrReplacingIt) {
  using namespace quader;
  const QuaderResolvedNudgeRequest request{
      .axis = QuaderTransformAxis::X, .move_delta = {1.0F, 0.0F, 0.0F}};
  ASSERT_TRUE(editor_.modeling().nudge_selection(request).ok());
  const QMat4 transform = session_.scene().world_transform_matrix(object_.entity);
  ASSERT_TRUE(editor_.modeling().begin_preview("half_cut").ok());
  ASSERT_TRUE(editor_.tools().active_settings().preview_active);
  const auto before = test::capture_command_observable_state(editor_);
  EXPECT_FALSE(editor_.modeling().can_nudge_selection());
  for (const bool duplicate : {false, true}) {
    SCOPED_TRACE(duplicate);
    auto pending_request = request;
    pending_request.duplicate_selected = duplicate;
    const auto rejected = editor_.modeling().nudge_selection(pending_request);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(test::capture_command_observable_state(editor_), before);
  }
  const auto repeated = editor_.history().repeat_last_action();
  EXPECT_FALSE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(test::capture_command_observable_state(editor_), before);
  ASSERT_TRUE(editor_.modeling().cancel_preview().ok());
  ASSERT_TRUE(editor_.history().repeat_last_action().ok());
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity),
                     scene_transform_multiply(
                         compose_transform(QTransform{.location = request.move_delta}), transform));
  expect_authored_vertices_unchanged();
}

TEST_F(MeshObjectTransformTests, NudgeRejectsLivePropertyDraftWithoutAcceptingItsHistory) {
  using namespace quader;
  const auto light = editor_.scene().create_point_light();
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(editor_.scene().select(light.value().id).ok());
  const QuaderResolvedNudgeRequest request{
      .axis = QuaderTransformAxis::X, .move_delta = {1.0F, 0.0F, 0.0F}};
  ASSERT_TRUE(editor_.modeling().nudge_selection(request).ok());
  const QMat4 transform = session_.scene().world_transform_matrix(light.value().id);
  const auto edit = editor_.scene().begin_light_color_edit(light.value().id);
  ASSERT_TRUE(edit.ok()) << edit.error_message();
  ASSERT_TRUE(editor_.scene().update_light_color_edit(edit.value(), {0.125F, 0.25F, 0.5F}).ok());
  const auto before = test::capture_command_observable_state(editor_);
  EXPECT_FALSE(editor_.modeling().can_nudge_selection());
  for (const bool duplicate : {false, true}) {
    SCOPED_TRACE(duplicate);
    auto pending_request = request;
    pending_request.duplicate_selected = duplicate;
    const auto rejected = editor_.modeling().nudge_selection(pending_request);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(test::capture_command_observable_state(editor_), before);
  }
  const auto repeated = editor_.history().repeat_last_action();
  EXPECT_FALSE(repeated.ok());
  EXPECT_FALSE(repeated.changed());
  EXPECT_EQ(test::capture_command_observable_state(editor_), before);
  ASSERT_TRUE(editor_.scene().finish_light_color_edit(edit.value(), true).ok());
  ASSERT_TRUE(editor_.history().repeat_last_action().ok());
  expect_matrix_near(session_.scene().world_transform_matrix(light.value().id),
                     scene_transform_multiply(
                         compose_transform(QTransform{.location = request.move_delta}), transform));
}

enum class DuplicateNudgeSelection { Mesh, Light, Mixed, Group };

class SceneDuplicateNudgeTests
    : public MeshObjectTransformTests,
      public ::testing::WithParamInterface<DuplicateNudgeSelection> {
protected:
  void SetUp() override {
    MeshObjectTransformTests::SetUp();
    if (HasFatalFailure()) {
      return;
    }
    using namespace quader;
    ASSERT_TRUE(editor_.scene().set_transform(object_.entity,
        {.location = {3.0F, -2.0F, 1.0F},
         .rotation_euler = radians({15.0F, 25.0F, -35.0F})}).ok());
    if (GetParam() != DuplicateNudgeSelection::Mesh) {
      const auto light = editor_.scene().create_point_light();
      ASSERT_TRUE(light.ok()) << light.error_message();
      ASSERT_TRUE(editor_.scene().set_transform(light.value().id,
          {.location = {-4.0F, 3.0F, 2.0F}}).ok());
      if (GetParam() == DuplicateNudgeSelection::Light) {
        ASSERT_TRUE(editor_.scene().select(light.value().id).ok());
      } else {
        ASSERT_TRUE(editor_.scene().select(
            {object_.entity, light.value().id}, object_.entity).ok());
      }
      if (GetParam() == DuplicateNudgeSelection::Group) {
        const auto group = editor_.scene().group_selection("Nudge Group");
        ASSERT_TRUE(group.ok()) << group.error_message();
        ASSERT_TRUE(editor_.scene().select(group.value().id).ok());
        ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
      }
    }
    ASSERT_TRUE(editor_.tools().activate(ViewportToolId::Scale).ok());
  }

  auto capture_replay_state() const {
    using namespace quader::test;
    const auto state = capture_command_observable_state(editor_);
    return std::tuple{session_.scene().entities(),
                      session_.scene().selected_entities(),
                      session_.scene().primary_selection(),
                      editor_.selection().summary().mode,
                      state.domain(CommandObservableDomain::Scene),
                      state.domain(CommandObservableDomain::Geometry)};
  }

  void expect_clone_tree(quader::EntityId source, quader::EntityId clone,
                         const quader::QMat4 &delta) const {
    using namespace quader;
    ASSERT_NE(clone, source);
    ASSERT_TRUE(session_.scene().exists(clone));
    EXPECT_EQ(session_.scene().kind(clone), session_.scene().kind(source));
    expect_matrix_near(session_.scene().world_transform_matrix(clone),
        scene_transform_multiply(delta, session_.scene().world_transform_matrix(source)));
    if (const MeshComponent *source_mesh = session_.scene().mesh(source)) {
      const MeshComponent *clone_mesh = session_.scene().mesh(clone);
      ASSERT_NE(clone_mesh, nullptr);
      const auto *source_asset = session_.assets().mesh(source_mesh->mesh);
      const auto *clone_asset = session_.assets().mesh(clone_mesh->mesh);
      ASSERT_NE(source_asset, nullptr);
      ASSERT_NE(clone_asset, nullptr);
      ASSERT_EQ(clone_asset->geometry().vertices.size(), source_asset->geometry().vertices.size());
      EXPECT_EQ(clone_asset->geometry().indices, source_asset->geometry().indices);
      for (std::size_t index = 0; index < source_asset->geometry().vertices.size(); ++index) {
        expect_vector_near(clone_asset->geometry().vertices[index].position,
                           source_asset->geometry().vertices[index].position);
      }
    }
    const auto source_children = session_.scene().children(source);
    const auto clone_children = session_.scene().children(clone);
    ASSERT_EQ(clone_children.size(), source_children.size());
    for (std::size_t index = 0; index < source_children.size(); ++index) {
      expect_clone_tree(source_children[index], clone_children[index], delta);
    }
  }
};

TEST_P(SceneDuplicateNudgeTests, EachMoveOrRotationAndRepeatClonesAsOneReversibleStep) {
  using namespace quader;
  const auto sources = session_.scene().selected_entities();
  const auto source_entities = session_.scene().entities();
  std::vector<QMat4> source_transforms;
  for (const EntityId entity : source_entities) {
    source_transforms.push_back(session_.scene().world_transform_matrix(entity));
  }
  const std::size_t cloned_count = GetParam() == DuplicateNudgeSelection::Group ? 3U
      : GetParam() == DuplicateNudgeSelection::Mixed ? 2U : 1U;
  for (const auto tool : {QuaderTransformTool::Move, QuaderTransformTool::Rotate}) {
    SCOPED_TRACE(static_cast<int>(tool));
    const auto before = capture_replay_state();
    const auto undo_count = session_.history().undo_count();
    const QVec3 pivot{1.0F, 2.0F, -1.0F};
    const QuaderResolvedNudgeRequest request{
        .tool = tool,
        .axis = QuaderTransformAxis::Z,
        .pivot = pivot,
        .move_delta = tool == QuaderTransformTool::Move ? QVec3{0.0F, 0.0F, 2.0F} : QVec3{},
        .rotate_angle_degrees = tool == QuaderTransformTool::Rotate ? 30.0 : 0.0,
        .duplicate_selected = true,
    };
    const QMat4 delta = tool == QuaderTransformTool::Move
        ? compose_transform(QTransform{.location = request.move_delta})
        : from_glm(glm::translate(glm::mat4{1.0F}, to_glm(pivot)) *
            glm::mat4_cast(to_glm(quat_from_euler_radians(radians({0.0F, 0.0F, 30.0F})))) *
            glm::translate(glm::mat4{1.0F}, -to_glm(pivot)));
    const auto nudged = editor_.modeling().nudge_selection(request);
    ASSERT_TRUE(nudged.ok()) << nudged.error_message();
    EXPECT_TRUE(nudged.changed());
    EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
    EXPECT_EQ(session_.scene().entities().size(), source_entities.size() + cloned_count);
    const auto first_clones = session_.scene().selected_entities();
    ASSERT_EQ(first_clones.size(), sources.size());
    const auto expect_clones = [&](const std::vector<EntityId> &clones, const QMat4 &world_delta) {
      for (const EntityId source : sources) {
        const auto clone = std::find_if(clones.begin(), clones.end(), [&](EntityId entity) {
          return session_.scene().kind(entity) == session_.scene().kind(source);
        });
        ASSERT_NE(clone, clones.end());
        expect_clone_tree(source, *clone, world_delta);
      }
      for (std::size_t index = 0; index < source_entities.size(); ++index) {
        expect_matrix_near(session_.scene().world_transform_matrix(source_entities[index]),
                           source_transforms[index]);
      }
      expect_authored_vertices_unchanged();
      EXPECT_EQ(editor_.tools().active_tool(), "scale");
    };
    expect_clones(first_clones, delta);
    const auto once = capture_replay_state();
    const auto repeated = editor_.history().repeat_last_action();
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_TRUE(repeated.changed());
    EXPECT_EQ(session_.history().undo_count(), undo_count + 2U);
    EXPECT_EQ(session_.scene().entities().size(), source_entities.size() + 2U * cloned_count);
    const auto second_clones = session_.scene().selected_entities();
    ASSERT_EQ(second_clones.size(), sources.size());
    for (const EntityId clone : second_clones) {
      EXPECT_EQ(std::find(first_clones.begin(), first_clones.end(), clone), first_clones.end());
    }
    expect_clones(first_clones, delta);
    expect_clones(second_clones, scene_transform_multiply(delta, delta));
    const auto twice = capture_replay_state();
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_EQ(session_.history().undo_count(), undo_count + 1U);
    EXPECT_EQ(capture_replay_state(), once);
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_EQ(session_.history().undo_count(), undo_count);
    EXPECT_EQ(capture_replay_state(), before);
    ASSERT_TRUE(editor_.history().redo().ok());
    EXPECT_EQ(capture_replay_state(), once);
    ASSERT_TRUE(editor_.history().redo().ok());
    EXPECT_EQ(capture_replay_state(), twice);
    ASSERT_TRUE(editor_.history().undo().ok());
    ASSERT_TRUE(editor_.history().undo().ok());
  }
}

INSTANTIATE_TEST_SUITE_P(ObjectAndGroup, SceneDuplicateNudgeTests,
    ::testing::Values(DuplicateNudgeSelection::Mesh, DuplicateNudgeSelection::Light,
                      DuplicateNudgeSelection::Mixed, DuplicateNudgeSelection::Group),
    [](const ::testing::TestParamInfo<DuplicateNudgeSelection> &info) {
      switch (info.param) {
      case DuplicateNudgeSelection::Mesh: return "Mesh";
      case DuplicateNudgeSelection::Light: return "Light";
      case DuplicateNudgeSelection::Mixed: return "Mixed";
      case DuplicateNudgeSelection::Group: return "Group";
      }
      return "Unknown";
    });

TEST_F(MeshObjectTransformTests, DuplicateNudgePublicationFailurePreservesSceneRedoAndRepeat) {
  using namespace quader;
  const auto light = editor_.scene().create_point_light();
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(editor_.scene().select({object_.entity, light.value().id}, object_.entity).ok());
  const QuaderResolvedNudgeRequest request{
      .axis = QuaderTransformAxis::X, .move_delta = {1.0F, 0.0F, 0.0F}};
  ASSERT_TRUE(editor_.modeling().nudge_selection(request).ok());
  const QMat4 expected = session_.scene().world_transform_matrix(object_.entity);
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto before = test::capture_command_observable_state(editor_);
  const auto entities = session_.scene().entities();
  for (const auto failure : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                             ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(failure));
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
    const auto rejected = editor_.modeling().nudge_selection({
        .axis = QuaderTransformAxis::Y,
        .move_delta = {0.0F, 2.0F, 0.0F},
        .duplicate_selected = true});
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    EXPECT_EQ(test::capture_command_observable_state(editor_), before);
    EXPECT_EQ(session_.scene().entities(), entities);
    EXPECT_TRUE(editor_.history().can_redo());
  }
  ASSERT_TRUE(editor_.history().repeat_last_action().ok());
  EXPECT_EQ(session_.scene().entities(), entities);
  expect_matrix_near(session_.scene().world_transform_matrix(object_.entity), expected);
}

TEST_F(MeshObjectTransformTests, DuplicateNudgeUnrepresentableRotationRemovesAllNewClones) {
  using namespace quader;
  const auto parent = editor_.scene().create_empty();
  ASSERT_TRUE(parent.ok()) << parent.error_message();
  ASSERT_TRUE(editor_.scene().set_transform(parent.value().id, {.scale = {2.0F, 1.0F, 1.0F}}).ok());
  ASSERT_TRUE(editor_.scene().reparent(object_.entity, parent.value().id, false).ok());
  const auto light = editor_.scene().create_point_light();
  ASSERT_TRUE(light.ok()) << light.error_message();
  ASSERT_TRUE(editor_.scene().select({object_.entity, light.value().id}, object_.entity).ok());
  const auto before = test::capture_command_observable_state(editor_);
  const auto entities = session_.scene().entities();
  const auto rejected = editor_.modeling().nudge_selection({
      .tool = QuaderTransformTool::Rotate,
      .axis = QuaderTransformAxis::Z,
      .rotate_angle_degrees = 45.0,
      .duplicate_selected = true});
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  const auto after = test::capture_command_observable_state(editor_);
  EXPECT_EQ(after, before)
      << test::diff_command_observable_state(before, after).summary
      << "\nBefore revisions:\n" << before.domain(test::CommandObservableDomain::Revisions).canonical
      << "\nAfter revisions:\n" << after.domain(test::CommandObservableDomain::Revisions).canonical;
  EXPECT_EQ(session_.scene().entities(), entities);
}

} // namespace
