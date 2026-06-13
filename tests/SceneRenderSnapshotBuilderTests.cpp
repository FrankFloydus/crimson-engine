#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

import quader.app.scene_render_snapshot_builder;
import quader.scene.scene;

namespace {

constexpr float tolerance = 0.001f;

void expect_near_vec3(quader::Vec3 actual, quader::Vec3 expected)
{
    EXPECT_NEAR(expected.x, actual.x, tolerance);
    EXPECT_NEAR(expected.y, actual.y, tolerance);
    EXPECT_NEAR(expected.z, actual.z, tolerance);
}

} // namespace

TEST(SceneRenderSnapshotBuilder, SnapshotRevisionTracksSceneRenderRevision)
{
    quader::Scene scene;

    const quader::SceneRenderSnapshot empty_snapshot = quader::build_scene_render_snapshot(scene);
    EXPECT_EQ(scene.render_revision(), empty_snapshot.revision);

    const uint64_t previous_revision = empty_snapshot.revision;
    (void)scene.create_cube();

    const quader::SceneRenderSnapshot renderable_snapshot = quader::build_scene_render_snapshot(scene);
    EXPECT_EQ(scene.render_revision(), renderable_snapshot.revision);
    EXPECT_GT(renderable_snapshot.revision, previous_revision);
}

TEST(SceneRenderSnapshotBuilder, UsesEntityIdsAndTransformDrivenLightDirection)
{
    quader::Scene scene;

    quader::TransformComponent light_transform;
    light_transform.set_rotation_degrees({ 0.0f, 90.0f, 0.0f });
    const quader::SceneEntityId directional_light =
        scene.create_directional_light("Directional Light", light_transform);

    quader::TransformComponent point_transform;
    point_transform.position = { 3.0f, 4.0f, 5.0f };
    const quader::SceneEntityId point_light = scene.create_point_light("Point Light", point_transform);
    quader::PointLightComponent point_settings = *scene.point_light(point_light);
    point_settings.settings.range = 12.5f;
    point_settings.settings.falloff_mode = quader::PointLightFalloffMode::inverse_squared;
    ASSERT_TRUE(scene.set_point_light(point_light, point_settings));

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    ASSERT_EQ(1U, snapshot.directional_lights.size());
    EXPECT_EQ(directional_light.value, snapshot.directional_lights[0].entity_id.value);
    expect_near_vec3(snapshot.directional_lights[0].direction, { -1.0f, 0.0f, 0.0f });
    expect_near_vec3(snapshot.directional_lights[0].right, { 0.0f, 0.0f, -1.0f });
    expect_near_vec3(snapshot.directional_lights[0].up, { 0.0f, 1.0f, 0.0f });

    ASSERT_EQ(1U, snapshot.point_lights.size());
    EXPECT_EQ(point_light.value, snapshot.point_lights[0].entity_id.value);
    expect_near_vec3(snapshot.point_lights[0].position, { 3.0f, 4.0f, 5.0f });
    EXPECT_FLOAT_EQ(12.5f, snapshot.point_lights[0].settings.range);
    EXPECT_EQ(quader::PointLightFalloffMode::inverse_squared, snapshot.point_lights[0].settings.falloff_mode);
}

TEST(SceneRenderSnapshotBuilder, CarriesEnvironmentSettings)
{
    quader::Scene scene;

    quader::EnvironmentLightingSettings settings;
    settings.intensity = 15000.0f;
    settings.rotation_degrees = -45.0f;
    settings.show_skybox = false;
    scene.set_environment_settings(settings);

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    EXPECT_FLOAT_EQ(15000.0f, snapshot.environment.intensity);
    EXPECT_FLOAT_EQ(-45.0f, snapshot.environment.rotation_degrees);
    EXPECT_FALSE(snapshot.environment.show_skybox);
}

TEST(SceneRenderSnapshotBuilder, CarriesPostProcessingSettings)
{
    quader::Scene scene;

    quader::PostProcessingSettings settings;
    settings.bloom.enabled = true;
    settings.bloom.strength = 0.42f;
    settings.bloom.quality = quader::BloomQuality::high;
    settings.ambient_occlusion.enabled = true;
    settings.ambient_occlusion.algorithm = quader::AmbientOcclusionAlgorithm::ground_truth;
    settings.ambient_occlusion.radius = 0.85f;
    settings.ambient_occlusion.gtao.sample_slice_count = 6;
    scene.set_post_processing_settings(settings);

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    EXPECT_TRUE(snapshot.post_processing.bloom.enabled);
    EXPECT_FLOAT_EQ(0.42f, snapshot.post_processing.bloom.strength);
    EXPECT_EQ(quader::BloomQuality::high, snapshot.post_processing.bloom.quality);
    EXPECT_TRUE(snapshot.post_processing.ambient_occlusion.enabled);
    EXPECT_EQ(
        quader::AmbientOcclusionAlgorithm::ground_truth,
        snapshot.post_processing.ambient_occlusion.algorithm);
    EXPECT_FLOAT_EQ(0.85f, snapshot.post_processing.ambient_occlusion.radius);
    EXPECT_EQ(6U, snapshot.post_processing.ambient_occlusion.gtao.sample_slice_count);
}

TEST(SceneRenderSnapshotBuilder, CarriesEmissiveMaterialSettings)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();

    quader::PbrMaterialSettings material;
    material.emission_color = { 0.25f, 0.5f, 1.0f };
    material.emission_texture.source_path = "textures/neon.png";
    material.emission_intensity = 650.0f;
    material.emission_exposure_weight = 0.0f;
    ASSERT_TRUE(scene.set_material(cube, material));

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    ASSERT_EQ(1U, snapshot.meshes.size());
    const quader::PbrMaterialSettings& snapshot_material = snapshot.meshes[0].material;
    EXPECT_FLOAT_EQ(0.25f, snapshot_material.emission_color.r);
    EXPECT_FLOAT_EQ(0.5f, snapshot_material.emission_color.g);
    EXPECT_FLOAT_EQ(1.0f, snapshot_material.emission_color.b);
    EXPECT_EQ("textures/neon.png", snapshot_material.emission_texture.source_path);
    EXPECT_FLOAT_EQ(650.0f, snapshot_material.emission_intensity);
    EXPECT_FLOAT_EQ(0.0f, snapshot_material.emission_exposure_weight);
}

TEST(SceneRenderSnapshotBuilder, CarriesTransparentMaterialSettings)
{
    quader::Scene scene;
    const quader::SceneEntityId cube = scene.create_cube();

    quader::PbrMaterialSettings material;
    material.blend_mode = quader::PbrMaterialBlendMode::transparent;
    material.transparency_mode = quader::PbrMaterialTransparencyMode::two_passes_two_sides;
    material.opacity = 0.42f;
    material.refraction_mode = quader::PbrMaterialRefractionMode::screen_space;
    material.refraction_type = quader::PbrMaterialRefractionType::thin;
    material.transmission = 0.75f;
    material.ior = 1.33f;
    ASSERT_TRUE(scene.set_material(cube, material));

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    ASSERT_EQ(1U, snapshot.meshes.size());
    const quader::PbrMaterialSettings& snapshot_material = snapshot.meshes[0].material;
    EXPECT_EQ(quader::PbrMaterialBlendMode::transparent, snapshot_material.blend_mode);
    EXPECT_EQ(quader::PbrMaterialTransparencyMode::two_passes_two_sides, snapshot_material.transparency_mode);
    EXPECT_FLOAT_EQ(0.42f, snapshot_material.opacity);
    EXPECT_EQ(quader::PbrMaterialRefractionMode::screen_space, snapshot_material.refraction_mode);
    EXPECT_EQ(quader::PbrMaterialRefractionType::thin, snapshot_material.refraction_type);
    EXPECT_FLOAT_EQ(0.75f, snapshot_material.transmission);
    EXPECT_FLOAT_EQ(1.33f, snapshot_material.ior);
}

TEST(SceneRenderSnapshotBuilder, CarriesDirectionalLightRollBasis)
{
    quader::Scene scene;

    quader::TransformComponent light_transform;
    light_transform.set_rotation_degrees({ 0.0f, 0.0f, 90.0f });
    const quader::SceneEntityId directional_light =
        scene.create_directional_light("Rolled Directional Light", light_transform);

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    ASSERT_EQ(1U, snapshot.directional_lights.size());
    EXPECT_EQ(directional_light.value, snapshot.directional_lights[0].entity_id.value);
    expect_near_vec3(snapshot.directional_lights[0].direction, { 0.0f, 0.0f, -1.0f });
    expect_near_vec3(snapshot.directional_lights[0].right, { 0.0f, 1.0f, 0.0f });
    expect_near_vec3(snapshot.directional_lights[0].up, { -1.0f, 0.0f, 0.0f });
}

TEST(SceneRenderSnapshotBuilder, PlaneTransformEditsReachRenderSnapshot)
{
    quader::Scene scene;
    const quader::SceneEntityId plane = scene.create_plane();

    quader::TransformComponent transform;
    transform.position = { 2.0f, 3.0f, 4.0f };
    transform.set_rotation_degrees({ 15.0f, 30.0f, 45.0f });
    transform.scale = { 2.0f, 1.0f, 3.0f };
    ASSERT_TRUE(scene.set_transform(plane, transform));

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    ASSERT_EQ(1U, snapshot.meshes.size());
    EXPECT_EQ(plane.value, snapshot.meshes[0].entity_id.value);
    EXPECT_EQ(quader::ViewportMeshKind::plane, snapshot.meshes[0].mesh_kind);
    expect_near_vec3(snapshot.meshes[0].position, { 2.0f, 3.0f, 4.0f });
    expect_near_vec3(snapshot.meshes[0].rotation_degrees, { 15.0f, 30.0f, 45.0f });
    expect_near_vec3(snapshot.meshes[0].scale, { 2.0f, 1.0f, 3.0f });
}
