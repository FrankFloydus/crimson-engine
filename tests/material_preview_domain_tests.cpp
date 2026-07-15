////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "math/glm_bridge.hpp"
#include "render/material_preview_mesh_library.hpp"
#include "render/material_preview_renderer.hpp"
#include "viewport/material_preview_state.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <vector>

using namespace quader;

namespace {

constexpr std::array<MaterialPreviewMeshKind, 5> kPreviewMeshKinds{
    MaterialPreviewMeshKind::Sphere,
    MaterialPreviewMeshKind::Box,
    MaterialPreviewMeshKind::RoundedBox,
    MaterialPreviewMeshKind::Cylinder,
    MaterialPreviewMeshKind::Plane,
};

bool finite(QVec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

void expect_mesh_is_finite_and_normalized(
    const MaterialPreviewMeshData &mesh) {
  ASSERT_FALSE(mesh.vertices.empty());
  ASSERT_FALSE(mesh.indices.empty());
  ASSERT_EQ(mesh.indices.size() % 3u, 0u);

  for (const MaterialPreviewVertex &vertex : mesh.vertices) {
    EXPECT_TRUE(finite(vertex.position));
    EXPECT_TRUE(finite(vertex.normal));
    EXPECT_TRUE(finite(vertex.uv));
    EXPECT_TRUE(finite(vertex.tangent_frame.tangent));
    EXPECT_TRUE(finite(vertex.tangent_frame.bitangent));
    EXPECT_LE(length(vertex.position), 1.00001f);
    EXPECT_GE(vertex.uv.x, -0.00001f);
    EXPECT_GE(vertex.uv.y, -0.00001f);
    EXPECT_LE(vertex.uv.x, 1.00001f);
    EXPECT_LE(vertex.uv.y, 1.00001f);
    EXPECT_NEAR(length(vertex.normal), 1.0f, 0.0001f);
    EXPECT_NEAR(length(vertex.tangent_frame.tangent), 1.0f, 0.0001f);
    EXPECT_NEAR(length(vertex.tangent_frame.bitangent), 1.0f, 0.0001f);
    EXPECT_NEAR(dot(vertex.normal, vertex.tangent_frame.tangent), 0.0f,
                0.0001f);
    EXPECT_NEAR(dot(vertex.normal, vertex.tangent_frame.bitangent), 0.0f,
                0.0001f);
    EXPECT_NEAR(dot(vertex.tangent_frame.tangent,
                    vertex.tangent_frame.bitangent),
                0.0f, 0.0001f);
    EXPECT_NEAR(std::abs(dot(cross(vertex.tangent_frame.tangent,
                                   vertex.tangent_frame.bitangent),
                              vertex.normal)),
                1.0f, 0.0001f);
  }

  for (std::size_t index = 0u; index < mesh.indices.size(); index += 3u) {
    const std::uint32_t index_a = mesh.indices[index];
    const std::uint32_t index_b = mesh.indices[index + 1u];
    const std::uint32_t index_c = mesh.indices[index + 2u];
    ASSERT_LT(index_a, mesh.vertices.size());
    ASSERT_LT(index_b, mesh.vertices.size());
    ASSERT_LT(index_c, mesh.vertices.size());
    const MaterialPreviewVertex &a = mesh.vertices[index_a];
    const MaterialPreviewVertex &b = mesh.vertices[index_b];
    const MaterialPreviewVertex &c = mesh.vertices[index_c];
    const QVec3 face_cross =
        cross(b.position - a.position, c.position - a.position);
    EXPECT_GT(length_squared(face_cross), 0.0000000001f);
    EXPECT_GT(dot(face_cross, a.normal + b.normal + c.normal), 0.0f);
  }
}

void expect_meshes_equal(const MaterialPreviewMeshData &left,
                         const MaterialPreviewMeshData &right) {
  ASSERT_EQ(left.vertices.size(), right.vertices.size());
  ASSERT_EQ(left.indices, right.indices);
  for (std::size_t index = 0u; index < left.vertices.size(); ++index) {
    const MaterialPreviewVertex &a = left.vertices[index];
    const MaterialPreviewVertex &b = right.vertices[index];
    EXPECT_FLOAT_EQ(a.position.x, b.position.x);
    EXPECT_FLOAT_EQ(a.position.y, b.position.y);
    EXPECT_FLOAT_EQ(a.position.z, b.position.z);
    EXPECT_FLOAT_EQ(a.normal.x, b.normal.x);
    EXPECT_FLOAT_EQ(a.normal.y, b.normal.y);
    EXPECT_FLOAT_EQ(a.normal.z, b.normal.z);
    EXPECT_FLOAT_EQ(a.uv.x, b.uv.x);
    EXPECT_FLOAT_EQ(a.uv.y, b.uv.y);
    EXPECT_FLOAT_EQ(a.tangent_frame.tangent.x,
                    b.tangent_frame.tangent.x);
    EXPECT_FLOAT_EQ(a.tangent_frame.tangent.y,
                    b.tangent_frame.tangent.y);
    EXPECT_FLOAT_EQ(a.tangent_frame.tangent.z,
                    b.tangent_frame.tangent.z);
    EXPECT_FLOAT_EQ(a.tangent_frame.bitangent.x,
                    b.tangent_frame.bitangent.x);
    EXPECT_FLOAT_EQ(a.tangent_frame.bitangent.y,
                    b.tangent_frame.bitangent.y);
    EXPECT_FLOAT_EQ(a.tangent_frame.bitangent.z,
                    b.tangent_frame.bitangent.z);
  }
}

} // namespace

TEST(MaterialPreviewDomainTests,
     MeshLibraryBuildsFiniteNormalizedOutwardGeometryForEveryShape) {
  const MaterialPreviewMeshLibrary library;

  for (MaterialPreviewMeshKind kind : kPreviewMeshKinds) {
    expect_mesh_is_finite_and_normalized(library.mesh(kind));
  }
}

TEST(MaterialPreviewDomainTests,
     MeshLibraryRetainsDistinctTopologiesAndCurvedRoundedBoxNormals) {
  const MaterialPreviewMeshLibrary library;
  const MaterialPreviewMeshData &sphere =
      library.mesh(MaterialPreviewMeshKind::Sphere);
  const MaterialPreviewMeshData &box =
      library.mesh(MaterialPreviewMeshKind::Box);
  const MaterialPreviewMeshData &rounded_box =
      library.mesh(MaterialPreviewMeshKind::RoundedBox);
  const MaterialPreviewMeshData &cylinder =
      library.mesh(MaterialPreviewMeshKind::Cylinder);
  const MaterialPreviewMeshData &plane =
      library.mesh(MaterialPreviewMeshKind::Plane);

  EXPECT_GT(sphere.vertices.size(), cylinder.vertices.size());
  EXPECT_EQ(box.vertices.size(), 24u);
  EXPECT_GT(rounded_box.vertices.size(), box.vertices.size());
  EXPECT_GT(cylinder.vertices.size(), box.vertices.size());
  EXPECT_EQ(plane.vertices.size(), 4u);

  bool found_curved_normal = false;
  for (const MaterialPreviewVertex &vertex : rounded_box.vertices) {
    const int significant_components =
        (std::abs(vertex.normal.x) > 0.1f ? 1 : 0) +
        (std::abs(vertex.normal.y) > 0.1f ? 1 : 0) +
        (std::abs(vertex.normal.z) > 0.1f ? 1 : 0);
    found_curved_normal = found_curved_normal || significant_components >= 2;
  }
  EXPECT_TRUE(found_curved_normal);
}

TEST(MaterialPreviewDomainTests, MeshLibraryOutputIsDeterministic) {
  const MaterialPreviewMeshLibrary first;
  const MaterialPreviewMeshLibrary second;

  for (MaterialPreviewMeshKind kind : kPreviewMeshKinds) {
    expect_meshes_equal(first.mesh(kind), second.mesh(kind));
  }
}

TEST(MaterialPreviewDomainTests,
     StateDefaultsToSphereRenderedAndPerspectiveOnly) {
  const MaterialPreviewState state;

  EXPECT_EQ(state.mesh_kind(), MaterialPreviewMeshKind::Sphere);
  EXPECT_EQ(state.shading_mode(), MaterialPreviewShadingMode::Rendered);
  EXPECT_EQ(state.projection(), MaterialPreviewProjection::Perspective);
  EXPECT_FLOAT_EQ(state.yaw_radians(), 0.0f);
  EXPECT_FLOAT_EQ(state.pitch_radians(), 0.0f);
  EXPECT_FLOAT_EQ(state.camera_distance(),
                  MaterialPreviewState::kDefaultCameraDistance);
  EXPECT_FLOAT_EQ(state.pan_offset().x, 0.0f);
  EXPECT_FLOAT_EQ(state.pan_offset().y, 0.0f);
  EXPECT_EQ(state.environment_id(), "lightroom_14b");
  EXPECT_FALSE(state.show_environment_background());
  EXPECT_FLOAT_EQ(state.environment_rotation_radians(), 0.0f);
  EXPECT_EQ(state.revision(), 1u);
}

TEST(MaterialPreviewDomainTests,
     RouterAcceptsOnlyActiveFiniteTumbleAndClampsPitch) {
  MaterialPreviewState state;
  const MaterialPreviewInputRouter router;

  MaterialPreviewInteractionResult result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .tumble_delta_pixels = {40.0f, 50.0f},
                 .tumble_active = false,
             });
  EXPECT_FALSE(result.changed);

  result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .tumble_delta_pixels = {40.0f, 100000.0f},
                 .tumble_active = true,
             });
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.tumbled);
  EXPECT_NEAR(state.yaw_radians(),
              40.0f * MaterialPreviewState::kTumbleRadiansPerPixel,
              0.00001f);
  EXPECT_FLOAT_EQ(state.pitch_radians(),
                  MaterialPreviewState::kMaximumPitchRadians);
  const QQuat rotation = state.object_rotation();
  EXPECT_NEAR(rotation.x * rotation.x + rotation.y * rotation.y +
                  rotation.z * rotation.z + rotation.w * rotation.w,
              1.0f, 0.0001f);

  const float yaw_before_invalid_input = state.yaw_radians();
  result = router.route(
      state,
      MaterialPreviewInteractionFrame{
          .tumble_delta_pixels = {
              std::numeric_limits<float>::quiet_NaN(), 1.0f},
          .tumble_active = true,
      });
  EXPECT_FALSE(result.changed);
  EXPECT_FLOAT_EQ(state.yaw_radians(), yaw_before_invalid_input);
}

TEST(MaterialPreviewDomainTests,
     UpwardTumbleDragMovesCameraFacingSurfaceUpward) {
  MaterialPreviewState state;
  const MaterialPreviewInputRouter router;

  const MaterialPreviewInteractionResult result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .tumble_delta_pixels = {0.0f, -20.0f},
                 .tumble_active = true,
             });

  EXPECT_TRUE(result.tumbled);
  EXPECT_NEAR(state.pitch_radians(),
              -20.0f * MaterialPreviewState::kTumbleRadiansPerPixel,
              0.00001f);
  const QVec3 transformed_front = transform_vector(
      compose_transform(QTransform{.rotation = state.object_rotation()}),
      {0.0f, 0.0f, 1.0f});
  EXPECT_GT(transformed_front.y, 0.0f)
      << "the renderer-facing mesh surface must follow an upward MMB drag";
}

TEST(MaterialPreviewDomainTests,
     HorizontalTumblePreservesDisplayedUpAxisAfterPitch) {
  MaterialPreviewState state;
  const MaterialPreviewInputRouter router;
  (void)router.route(state, MaterialPreviewInteractionFrame{
                                .tumble_delta_pixels = {0.0f, -30.0f},
                                .tumble_active = true,
                            });
  const QVec3 up_before = transform_vector(
      compose_transform(QTransform{.rotation = state.object_rotation()}),
      {0.0f, 1.0f, 0.0f});
  const QVec3 front_before = transform_vector(
      compose_transform(QTransform{.rotation = state.object_rotation()}),
      {0.0f, 0.0f, 1.0f});
  const float pitch_before = state.pitch_radians();

  const MaterialPreviewInteractionResult result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .tumble_delta_pixels = {40.0f, 0.0f},
                 .tumble_active = true,
             });
  const QVec3 up_after = transform_vector(
      compose_transform(QTransform{.rotation = state.object_rotation()}),
      {0.0f, 1.0f, 0.0f});
  const QVec3 front_after = transform_vector(
      compose_transform(QTransform{.rotation = state.object_rotation()}),
      {0.0f, 0.0f, 1.0f});

  EXPECT_TRUE(result.tumbled);
  EXPECT_FLOAT_EQ(state.pitch_radians(), pitch_before);
  EXPECT_NEAR(up_after.x, up_before.x, 0.00001f);
  EXPECT_NEAR(up_after.y, up_before.y, 0.00001f);
  EXPECT_NEAR(up_after.z, up_before.z, 0.00001f);
  EXPECT_NEAR(up_after.x, 0.0f, 0.00001f);
  EXPECT_NE(front_after.x, front_before.x);
  EXPECT_NEAR(dot(up_after, front_after), 0.0f, 0.00001f);
}

TEST(MaterialPreviewDomainTests, RouterZoomsWithinFiniteDistanceClamps) {
  MaterialPreviewState state;
  const MaterialPreviewInputRouter router;

  MaterialPreviewInteractionResult result = router.route(
      state, MaterialPreviewInteractionFrame{.zoom_wheel_steps = 1.0f});
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.zoomed);
  EXPECT_LT(state.camera_distance(),
            MaterialPreviewState::kDefaultCameraDistance);

  result = router.route(
      state, MaterialPreviewInteractionFrame{.zoom_wheel_steps = 100000.0f});
  EXPECT_TRUE(result.zoomed);
  EXPECT_FLOAT_EQ(state.camera_distance(),
                  MaterialPreviewState::kMinimumCameraDistance);

  result = router.route(
      state,
      MaterialPreviewInteractionFrame{.zoom_wheel_steps = -100000.0f});
  EXPECT_TRUE(result.zoomed);
  EXPECT_FLOAT_EQ(state.camera_distance(),
                  MaterialPreviewState::kMaximumCameraDistance);

  result = router.route(
      state,
      MaterialPreviewInteractionFrame{
          .zoom_wheel_steps = std::numeric_limits<float>::infinity()});
  EXPECT_FALSE(result.changed);
  EXPECT_FLOAT_EQ(state.camera_distance(),
                  MaterialPreviewState::kMaximumCameraDistance);
}

TEST(MaterialPreviewDomainTests,
     RouterSeparatesMeshTumblePanAndEnvironmentRotation) {
  MaterialPreviewState state;
  const MaterialPreviewInputRouter router;

  MaterialPreviewInteractionResult result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .tumble_delta_pixels = {20.0f, -10.0f},
                 .pan_delta_pixels = {40.0f, 25.0f},
                 .environment_rotation_delta_pixels = 30.0f,
                 .tumble_active = true,
             });
  EXPECT_TRUE(result.tumbled);
  EXPECT_FALSE(result.panned);
  EXPECT_FALSE(result.environment_rotated);
  EXPECT_FLOAT_EQ(state.pan_offset().x, 0.0f);
  EXPECT_FLOAT_EQ(state.pan_offset().y, 0.0f);
  EXPECT_FLOAT_EQ(state.environment_rotation_radians(), 0.0f);

  const float yaw_after_tumble = state.yaw_radians();
  const float pitch_after_tumble = state.pitch_radians();
  result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .pan_delta_pixels = {40.0f, 25.0f},
                 .pan_active = true,
             });
  EXPECT_FALSE(result.tumbled);
  EXPECT_TRUE(result.panned);
  EXPECT_FALSE(result.environment_rotated);
  EXPECT_FLOAT_EQ(state.yaw_radians(), yaw_after_tumble);
  EXPECT_FLOAT_EQ(state.pitch_radians(), pitch_after_tumble);
  EXPECT_LT(state.pan_offset().x, 0.0f);
  EXPECT_GT(state.pan_offset().y, 0.0f);

  const QVec2 pan_after_pan = state.pan_offset();
  result = router.route(
      state, MaterialPreviewInteractionFrame{
                 .environment_rotation_delta_pixels = 30.0f,
                 .environment_rotation_active = true,
             });
  EXPECT_FALSE(result.tumbled);
  EXPECT_FALSE(result.panned);
  EXPECT_TRUE(result.environment_rotated);
  EXPECT_FLOAT_EQ(state.yaw_radians(), yaw_after_tumble);
  EXPECT_FLOAT_EQ(state.pitch_radians(), pitch_after_tumble);
  EXPECT_FLOAT_EQ(state.pan_offset().x, pan_after_pan.x);
  EXPECT_FLOAT_EQ(state.pan_offset().y, pan_after_pan.y);
  EXPECT_NEAR(state.environment_rotation_radians(),
              30.0f *
                  MaterialPreviewState::kEnvironmentRadiansPerPixel,
              0.00001f);

  const MaterialPreviewInteractionResult invalid = router.route(
      state,
      MaterialPreviewInteractionFrame{
          .pan_delta_pixels = {
              std::numeric_limits<float>::infinity(), 1.0f},
          .environment_rotation_delta_pixels =
              std::numeric_limits<float>::quiet_NaN(),
          .pan_active = true,
          .environment_rotation_active = true,
      });
  EXPECT_FALSE(invalid.changed);
}

TEST(MaterialPreviewDomainTests,
     EnvironmentSelectionAndBackgroundUsePackagedCatalog) {
  MaterialPreviewState state;
  const std::uint64_t initial_revision = state.revision();

  EXPECT_TRUE(state.set_environment("pillars_2k"));
  EXPECT_EQ(state.environment_id(), "pillars_2k");
  EXPECT_EQ(state.revision(), initial_revision + 1u);
  EXPECT_FALSE(state.set_environment("pillars_2k"));
  EXPECT_FALSE(state.set_environment("not_packaged"));
  EXPECT_EQ(state.environment_id(), "pillars_2k");

  EXPECT_TRUE(state.set_show_environment_background(true));
  EXPECT_TRUE(state.show_environment_background());
  EXPECT_FALSE(state.set_show_environment_background(true));
  EXPECT_TRUE(state.set_show_environment_background(false));
  EXPECT_FALSE(state.show_environment_background());
}

TEST(MaterialPreviewDomainTests,
     MeshChangesPreserveAndExplicitResetRestoresTheInspectionView) {
  MaterialPreviewState state;
  const MaterialPreviewInputRouter router;
  (void)router.route(state, MaterialPreviewInteractionFrame{
                                .tumble_delta_pixels = {12.0f, -8.0f},
                                .zoom_wheel_steps = 2.0f,
                                .tumble_active = true,
                            });
  const std::uint64_t revision_before_mesh_change = state.revision();
  const float yaw_before_mesh_change = state.yaw_radians();
  const float pitch_before_mesh_change = state.pitch_radians();
  const float distance_before_mesh_change = state.camera_distance();

  EXPECT_TRUE(state.set_mesh_kind(MaterialPreviewMeshKind::Cylinder));
  EXPECT_EQ(state.revision(), revision_before_mesh_change + 1u);
  EXPECT_FLOAT_EQ(state.yaw_radians(), yaw_before_mesh_change);
  EXPECT_FLOAT_EQ(state.pitch_radians(), pitch_before_mesh_change);
  EXPECT_FLOAT_EQ(state.camera_distance(), distance_before_mesh_change);
  EXPECT_FALSE(state.set_mesh_kind(MaterialPreviewMeshKind::Cylinder));

  (void)router.route(state, MaterialPreviewInteractionFrame{
                                .tumble_delta_pixels = {5.0f, 5.0f},
                                .pan_delta_pixels = {7.0f, -4.0f},
                                .environment_rotation_delta_pixels = 9.0f,
                                .zoom_wheel_steps = -2.0f,
                                .tumble_active = true,
                                .pan_active = true,
                                .environment_rotation_active = true,
                            });
  const MaterialPreviewInteractionResult result = router.route(
      state, MaterialPreviewInteractionFrame{.reset_requested = true});
  EXPECT_TRUE(result.changed);
  EXPECT_TRUE(result.reset);
  EXPECT_FLOAT_EQ(state.yaw_radians(), 0.0f);
  EXPECT_FLOAT_EQ(state.pitch_radians(), 0.0f);
  EXPECT_FLOAT_EQ(state.pan_offset().x, 0.0f);
  EXPECT_FLOAT_EQ(state.pan_offset().y, 0.0f);
  EXPECT_FLOAT_EQ(state.camera_distance(),
                  MaterialPreviewState::kDefaultCameraDistance);
  EXPECT_FLOAT_EQ(state.environment_rotation_radians(), 0.0f);
}

TEST(MaterialPreviewDomainTests, ShadingChangesHaveIndependentRevision) {
  MaterialPreviewState state;
  const std::uint64_t original_revision = state.revision();

  EXPECT_TRUE(
      state.set_shading_mode(MaterialPreviewShadingMode::Wireframe));
  EXPECT_EQ(state.revision(), original_revision + 1u);
  EXPECT_EQ(state.shading_mode(), MaterialPreviewShadingMode::Wireframe);
  EXPECT_FALSE(
      state.set_shading_mode(MaterialPreviewShadingMode::Wireframe));
  EXPECT_FALSE(state.set_mesh_kind(
      static_cast<MaterialPreviewMeshKind>(std::numeric_limits<int>::max())));
  EXPECT_FALSE(state.set_shading_mode(static_cast<MaterialPreviewShadingMode>(
      std::numeric_limits<int>::max())));
}

TEST(MaterialPreviewDomainTests,
     TextureSnapshotsRequireExactFiniteRgba8Storage) {
  const auto pixels = std::make_shared<const std::vector<std::byte>>(16u);
  MaterialPreviewTextureSnapshot texture{
      .identity_revision = 10u,
      .content_revision = 20u,
      .color_space = TextureColorSpace::Linear,
      .decoded_rgba8 = pixels,
      .width = 2u,
      .height = 2u,
  };
  EXPECT_TRUE(texture.present());

  texture.width = 3u;
  EXPECT_FALSE(texture.present());
  texture.width = std::numeric_limits<std::uint32_t>::max();
  texture.height = std::numeric_limits<std::uint32_t>::max();
  EXPECT_FALSE(texture.present());
  texture.decoded_rgba8.reset();
  EXPECT_FALSE(texture.present());
}

TEST(MaterialPreviewDomainTests,
     StandardSnapshotsCompareTypedMaterialAndTextureRevisions) {
  StandardMaterialPreviewSnapshot first;
  StandardMaterialPreviewSnapshot second;
  EXPECT_EQ(first, second);

  second.content_revision = 2u;
  EXPECT_NE(first, second);
  first.content_revision = 2u;
  ASSERT_NE(first.standard_material(), nullptr);
  ASSERT_NE(second.standard_material(), nullptr);
  second.standard_material()->normal_strength = 1.5f;
  EXPECT_NE(first, second);
  first.standard_material()->normal_strength = 1.5f;
  second.standard_material()->texture_transform.offset = {0.25f, -0.5f};
  EXPECT_NE(first, second);
  first.standard_material()->texture_transform.offset = {0.25f, -0.5f};
  second.standard_material()
      ->textures[static_cast<std::size_t>(
          MaterialPreviewTextureSlot::Normal)]
      .content_revision = 7u;
  EXPECT_NE(first, second);
}
