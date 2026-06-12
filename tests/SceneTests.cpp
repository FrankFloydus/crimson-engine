#include <gtest/gtest.h>

import quader.scene.scene;

TEST(Scene, DefaultSceneContainsCubeAndDirectionalLight)
{
    quader::Scene scene = quader::create_default_scene();

    EXPECT_EQ("Default Scene", scene.title());
    ASSERT_EQ(2u, scene.entities().size());

    const quader::SceneMeshComponent* mesh = quader::first_mesh_component(scene);
    ASSERT_NE(nullptr, mesh);
    EXPECT_EQ(quader::SceneMeshKind::cube, mesh->mesh_kind);

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
