#include <gtest/gtest.h>

#include <cmath>
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
    EXPECT_FLOAT_EQ(10000.0f, scene.point_light(point_light)->settings.intensity);
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
    scene.material(cube)->material.roughness = 0.42f;

    const std::optional<quader::SceneEntity> entity = scene.entity(cube);

    ASSERT_TRUE(entity.has_value());
    EXPECT_EQ(cube, entity->id);
    EXPECT_EQ("Snapshot Cube", entity->name);
    expect_near_vec3(entity->transform.position, { 1.0f, 2.0f, 3.0f });
    ASSERT_TRUE(entity->mesh.has_value());
    ASSERT_TRUE(entity->material.has_value());
    EXPECT_FLOAT_EQ(0.42f, entity->material->material.roughness);
}

TEST(Scene, RenderSnapshotUsesEntityIdsAndTransformDrivenLightDirection)
{
    quader::Scene scene;

    quader::TransformComponent light_transform;
    light_transform.set_rotation_degrees({ 0.0f, 90.0f, 0.0f });
    const quader::SceneEntityId directional_light =
        scene.create_directional_light("Directional Light", light_transform);

    quader::TransformComponent point_transform;
    point_transform.position = { 3.0f, 4.0f, 5.0f };
    const quader::SceneEntityId point_light = scene.create_point_light("Point Light", point_transform);
    scene.point_light(point_light)->settings.range = 12.5f;

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
}

TEST(Scene, RenderSnapshotCarriesDirectionalLightRollBasis)
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

TEST(Scene, PlaneTransformEditsReachRenderSnapshot)
{
    quader::Scene scene;
    const quader::SceneEntityId plane = scene.create_plane();

    quader::TransformComponent* transform = scene.transform(plane);
    ASSERT_NE(nullptr, transform);
    transform->position = { 2.0f, 3.0f, 4.0f };
    transform->set_rotation_degrees({ 15.0f, 30.0f, 45.0f });
    transform->scale = { 2.0f, 1.0f, 3.0f };

    const quader::SceneRenderSnapshot snapshot = quader::build_scene_render_snapshot(scene);

    ASSERT_EQ(1U, snapshot.meshes.size());
    EXPECT_EQ(plane.value, snapshot.meshes[0].entity_id.value);
    EXPECT_EQ(quader::ViewportMeshKind::plane, snapshot.meshes[0].mesh_kind);
    expect_near_vec3(snapshot.meshes[0].position, { 2.0f, 3.0f, 4.0f });
    expect_near_vec3(snapshot.meshes[0].rotation_degrees, { 15.0f, 30.0f, 45.0f });
    expect_near_vec3(snapshot.meshes[0].scale, { 2.0f, 1.0f, 3.0f });
}
