module;

#include <span>
#include <string>
#include <string_view>
#include <utility>

module quader.scene.scene;

namespace quader {

Scene::Scene()
    : Scene("Untitled Scene")
{
}

Scene::Scene(std::string title)
    : title_(std::move(title))
{
}

std::string_view Scene::title() const
{
    return title_;
}

void Scene::set_title(std::string title)
{
    title_ = std::move(title);
}

SceneEntity& Scene::add_entity(std::string name, SceneTransform transform)
{
    SceneEntity entity;
    entity.id = SceneEntityId { next_entity_id_++ };
    entity.name = std::move(name);
    entity.transform = transform;

    entities_.push_back(std::move(entity));
    return entities_.back();
}

std::span<SceneEntity> Scene::entities()
{
    return entities_;
}

std::span<const SceneEntity> Scene::entities() const
{
    return entities_;
}

Scene create_default_scene()
{
    Scene scene("Default Scene");

    SceneEntity& cube = scene.add_entity("Cube", SceneTransform {
        .position = { 0.0f, 1.0f, 0.0f },
    });
    cube.mesh = SceneMeshComponent {};

    SceneEntity& ground_plane = scene.add_entity("Ground Plane", SceneTransform {
        .scale = { 10.0f, 1.0f, 10.0f },
    });
    SceneMeshComponent ground_mesh;
    ground_mesh.mesh_kind = SceneMeshKind::plane;
    ground_mesh.material.albedo = { 0.55f, 0.58f, 0.56f };
    ground_mesh.material.roughness = 0.9f;
    ground_mesh.casts_shadows = false;
    ground_mesh.receives_shadows = true;
    ground_plane.mesh = ground_mesh;

    SceneEntity& key_light = scene.add_entity("Key Light");
    key_light.directional_light = SceneDirectionalLightComponent {};

    return scene;
}

SceneMeshComponent* first_mesh_component(Scene& scene)
{
    for (SceneEntity& entity : scene.entities()) {
        if (entity.mesh.has_value()) {
            return &*entity.mesh;
        }
    }

    return nullptr;
}

const SceneMeshComponent* first_mesh_component(const Scene& scene)
{
    for (const SceneEntity& entity : scene.entities()) {
        if (entity.mesh.has_value()) {
            return &*entity.mesh;
        }
    }

    return nullptr;
}

SceneDirectionalLightComponent* first_directional_light_component(Scene& scene)
{
    for (SceneEntity& entity : scene.entities()) {
        if (entity.directional_light.has_value()) {
            return &*entity.directional_light;
        }
    }

    return nullptr;
}

const SceneDirectionalLightComponent* first_directional_light_component(const Scene& scene)
{
    for (const SceneEntity& entity : scene.entities()) {
        if (entity.directional_light.has_value()) {
            return &*entity.directional_light;
        }
    }

    return nullptr;
}

} // namespace quader
