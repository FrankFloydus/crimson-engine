#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>

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

TEST(Scene, DefaultSceneStartsEmpty)
{
    const quader::Scene scene;

    EXPECT_EQ("Untitled Scene", scene.title());
    EXPECT_EQ(0u, scene.entity_count());
    EXPECT_TRUE(scene.entities().empty());
    EXPECT_TRUE(scene.environment_settings().enabled);
    EXPECT_FLOAT_EQ(30000.0f, scene.environment_settings().intensity);
    EXPECT_FALSE(scene.environment_settings().show_skybox);
    EXPECT_FALSE(scene.post_processing_settings().bloom.enabled);
    EXPECT_FALSE(scene.post_processing_settings().ambient_occlusion.enabled);
}

TEST(Scene, StoresClampedEnvironmentSettings)
{
    quader::Scene scene;

    quader::EnvironmentLightingSettings settings;
    settings.enabled = false;
    settings.intensity = -15.0f;
    settings.rotation_degrees = 540.0f;
    settings.show_skybox = false;
    settings.fallback_color = { 1.25f, 0.5f, -0.25f, 0.75f };

    scene.set_environment_settings(settings);

    const quader::EnvironmentLightingSettings stored = scene.environment_settings();
    EXPECT_FALSE(stored.enabled);
    EXPECT_FLOAT_EQ(0.0f, stored.intensity);
    EXPECT_FLOAT_EQ(180.0f, stored.rotation_degrees);
    EXPECT_FALSE(stored.show_skybox);
    EXPECT_FLOAT_EQ(1.0f, stored.fallback_color.r);
    EXPECT_FLOAT_EQ(0.5f, stored.fallback_color.g);
    EXPECT_FLOAT_EQ(0.0f, stored.fallback_color.b);
    EXPECT_FLOAT_EQ(0.75f, stored.fallback_color.a);
}

TEST(Scene, StoresClampedPostProcessingSettings)
{
    quader::Scene scene;

    quader::PostProcessingSettings settings;
    settings.bloom.enabled = true;
    settings.bloom.strength = 2.0f;
    settings.bloom.resolution = 1U;
    settings.bloom.levels = 64U;
    settings.bloom.highlight = -1.0f;
    settings.ambient_occlusion.enabled = true;
    settings.ambient_occlusion.radius = 12.0f;
    settings.ambient_occlusion.resolution_scale = 1.0f;
    settings.ambient_occlusion.ssct.sample_count = 0;

    scene.set_post_processing_settings(settings);

    const quader::PostProcessingSettings stored = scene.post_processing_settings();
    EXPECT_TRUE(stored.bloom.enabled);
    EXPECT_FLOAT_EQ(1.0f, stored.bloom.strength);
    EXPECT_EQ(8U, stored.bloom.resolution);
    EXPECT_EQ(11U, stored.bloom.levels);
    EXPECT_FLOAT_EQ(10.0f, stored.bloom.highlight);
    EXPECT_TRUE(stored.ambient_occlusion.enabled);
    EXPECT_FLOAT_EQ(10.0f, stored.ambient_occlusion.radius);
    EXPECT_FLOAT_EQ(1.0f, stored.ambient_occlusion.resolution_scale);
    EXPECT_EQ(4U, stored.ambient_occlusion.ssct.sample_count);
}

TEST(Scene, CreatesDefaultRenderableAndLightEntities)
{
    quader::Scene scene("Test Scene");

    const quader::SceneEntityId cube = scene.create_cube();
    const quader::SceneEntityId plane = scene.create_plane();
    const quader::SceneEntityId directional_light = scene.create_directional_light();
    const quader::SceneEntityId point_light = scene.create_point_light();

    EXPECT_EQ(4u, scene.entity_count());
    ASSERT_NE(nullptr, scene.transform(cube));
    ASSERT_NE(nullptr, scene.mesh(cube));
    ASSERT_NE(nullptr, scene.material(cube));
    EXPECT_EQ(quader::SceneMeshKind::cube, scene.mesh(cube)->mesh_kind);
    EXPECT_TRUE(scene.mesh(cube)->casts_shadows);
    EXPECT_TRUE(scene.mesh(cube)->receives_shadows);

    ASSERT_NE(nullptr, scene.mesh(plane));
    EXPECT_EQ(quader::SceneMeshKind::plane, scene.mesh(plane)->mesh_kind);

    ASSERT_NE(nullptr, scene.directional_light(directional_light));
    EXPECT_FLOAT_EQ(65000.0f, scene.directional_light(directional_light)->settings.intensity);

    ASSERT_NE(nullptr, scene.point_light(point_light));
    EXPECT_FLOAT_EQ(1000000.0f, scene.point_light(point_light)->settings.intensity);
    EXPECT_EQ(quader::PointLightFalloffMode::linear, scene.point_light(point_light)->settings.falloff_mode);
    EXPECT_TRUE(scene.point_light(point_light)->settings.shadow.enabled);
}

TEST(Scene, EntityIdsStayStableAndAreNotReusedAfterDestroy)
{
    quader::Scene scene;

    const quader::SceneEntityId first = scene.create_entity("First");
    const quader::SceneEntityId second = scene.create_entity("Second");

    EXPECT_TRUE(first.is_valid());
    EXPECT_TRUE(second.is_valid());
    EXPECT_LT(first.value, second.value);
    ASSERT_TRUE(scene.contains(first));
    ASSERT_TRUE(scene.destroy_entity(first));
    EXPECT_FALSE(scene.contains(first));

    const quader::SceneEntityId third = scene.create_entity("Third");
    EXPECT_LT(second.value, third.value);
    EXPECT_FALSE(scene.destroy_entity(first));
}

TEST(Scene, TransformRotationDegreesRoundTripsThroughQuaternion)
{
    quader::TransformComponent transform;

    transform.set_rotation_degrees({ 15.0f, 35.0f, -20.0f });

    expect_near_vec3(transform.get_rotation_degrees(), { 15.0f, 35.0f, -20.0f });
    const float length =
        std::sqrt(transform.rotation.x * transform.rotation.x +
            transform.rotation.y * transform.rotation.y +
            transform.rotation.z * transform.rotation.z +
            transform.rotation.w * transform.rotation.w);
    EXPECT_NEAR(1.0f, length, tolerance);
}

TEST(Scene, EntitySnapshotsExposeProjectOwnedComponentsWithoutOwningStorage)
{
    quader::Scene scene;

    quader::TransformComponent transform;
    transform.position = { 1.0f, 2.0f, 3.0f };
    const quader::SceneEntityId cube = scene.create_cube("Snapshot Cube", transform);
    quader::PbrMaterialSettings material_settings;
    material_settings.roughness = 0.42f;
    ASSERT_TRUE(scene.set_material(cube, material_settings));

    const std::optional<quader::SceneEntity> entity = scene.entity(cube);

    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(cube, entity->id);
    EXPECT_EQ("Snapshot Cube", entity->name);
    expect_near_vec3(entity->transform.position, { 1.0f, 2.0f, 3.0f });
    ASSERT_TRUE(entity->mesh.has_value());
    ASSERT_TRUE(entity->material.has_value());
    EXPECT_FLOAT_EQ(0.42f, entity->material->material.roughness);
}

TEST(Scene, RenderRevisionStartsValidAndIgnoresNonRenderingEdits)
{
    quader::Scene scene;

    const uint64_t initial_revision = scene.render_revision();
    EXPECT_NE(0ULL, initial_revision);

    scene.set_title("Renamed Scene");
    EXPECT_EQ(initial_revision, scene.render_revision());

    const quader::SceneEntityId empty_entity = scene.create_entity("Empty");
    EXPECT_EQ(initial_revision, scene.render_revision());

    quader::TransformComponent transform;
    transform.position = { 4.0f, 5.0f, 6.0f };
    EXPECT_TRUE(scene.set_transform(empty_entity, transform));
    EXPECT_EQ(initial_revision, scene.render_revision());

    EXPECT_FALSE(scene.set_material(empty_entity, quader::PbrMaterialSettings {}));
    EXPECT_EQ(initial_revision, scene.render_revision());

    EXPECT_TRUE(scene.destroy_entity(empty_entity));
    EXPECT_EQ(initial_revision, scene.render_revision());
}

TEST(Scene, RenderRevisionTracksRenderableSceneEdits)
{
    quader::Scene scene;

    uint64_t previous_revision = scene.render_revision();
    const quader::SceneEntityId cube = scene.create_cube();
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::TransformComponent transform;
    transform.position = { 2.0f, 3.0f, 4.0f };
    EXPECT_TRUE(scene.set_transform(cube, transform));
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::PbrMaterialSettings material;
    material.emission_intensity = 3.0f;
    EXPECT_TRUE(scene.set_material(cube, material));
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    EXPECT_TRUE(scene.set_material_texture_source(
        cube,
        quader::PbrMaterialTextureSlot::albedo,
        "textures/albedo.png"));
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::MeshShadowSettings shadows;
    shadows.casts_shadows = false;
    EXPECT_TRUE(scene.set_mesh_shadow_settings(cube, shadows));
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::EnvironmentLightingSettings environment;
    environment.intensity = 12000.0f;
    scene.set_environment_settings(environment);
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::PostProcessingSettings post_processing;
    post_processing.bloom.enabled = true;
    scene.set_post_processing_settings(post_processing);
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    const quader::SceneEntityId directional_light = scene.create_directional_light();
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::DirectionalLightSettings directional_settings;
    directional_settings.intensity = 18000.0f;
    EXPECT_TRUE(scene.set_directional_light(directional_light, directional_settings));
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    const quader::SceneEntityId point_light = scene.create_point_light();
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    quader::PointLightComponent point_settings = *scene.point_light(point_light);
    point_settings.settings.range = 6.0f;
    EXPECT_TRUE(scene.set_point_light(point_light, point_settings));
    EXPECT_GT(scene.render_revision(), previous_revision);

    previous_revision = scene.render_revision();
    EXPECT_TRUE(scene.destroy_entity(cube));
    EXPECT_GT(scene.render_revision(), previous_revision);
}
