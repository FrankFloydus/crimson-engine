#include <gtest/gtest.h>

import quader.scene.scene;

TEST(Scene, DefaultSceneContainsCubeGroundPlaneAndDirectionalLight)
{
    quader::Scene scene = quader::create_default_scene();

    EXPECT_EQ("Default Scene", scene.title());
    ASSERT_EQ(3u, scene.entities().size());

    const quader::SceneMeshComponent* mesh = quader::first_mesh_component(scene);
    ASSERT_NE(nullptr, mesh);
    EXPECT_EQ(quader::SceneMeshKind::cube, mesh->mesh_kind);

    const quader::SceneEntity& cube = scene.entities()[0];
    EXPECT_EQ("Cube", cube.name);
    EXPECT_FLOAT_EQ(1.0f, cube.transform.position.y);
    ASSERT_TRUE(cube.mesh.has_value());
    EXPECT_TRUE(cube.mesh->casts_shadows);
    EXPECT_TRUE(cube.mesh->receives_shadows);

    const quader::SceneEntity& ground_plane = scene.entities()[1];
    EXPECT_EQ("Ground Plane", ground_plane.name);
    ASSERT_TRUE(ground_plane.mesh.has_value());
    EXPECT_EQ(quader::SceneMeshKind::plane, ground_plane.mesh->mesh_kind);
    EXPECT_FLOAT_EQ(10.0f, ground_plane.transform.scale.x);
    EXPECT_FLOAT_EQ(1.0f, ground_plane.transform.scale.y);
    EXPECT_FLOAT_EQ(10.0f, ground_plane.transform.scale.z);
    EXPECT_FLOAT_EQ(0.55f, ground_plane.mesh->material.albedo.r);
    EXPECT_FALSE(ground_plane.mesh->casts_shadows);
    EXPECT_TRUE(ground_plane.mesh->receives_shadows);

    const quader::SceneDirectionalLightComponent* light = quader::first_directional_light_component(scene);
    ASSERT_NE(nullptr, light);
    EXPECT_FLOAT_EQ(65000.0f, light->settings.intensity);
}

TEST(Scene, AddsEntitiesWithStableIncreasingIds)
{
    quader::Scene scene("Test Scene");

    const quader::SceneEntityId first = scene.add_entity("First").id;
    const quader::SceneEntityId second = scene.add_entity("Second").id;

    EXPECT_TRUE(first.is_valid());
    EXPECT_TRUE(second.is_valid());
    EXPECT_LT(first.value, second.value);
}
