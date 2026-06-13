module;

#include <glm/ext/quaternion_float.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

module quader.app.scene_render_snapshot_builder;

import quader.scene.scene;

namespace quader {
namespace {

[[nodiscard]] glm::quat to_glm(Quaternion rotation)
{
    return glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
}

[[nodiscard]] Vec3 rotate_direction(Quaternion rotation, Vec3 direction)
{
    const glm::vec3 rotated = to_glm(rotation) * glm::vec3(direction.x, direction.y, direction.z);
    if (glm::dot(rotated, rotated) <= 0.000001f) {
        return direction;
    }

    const glm::vec3 normalized = glm::normalize(rotated);
    return { normalized.x, normalized.y, normalized.z };
}

[[nodiscard]] ViewportMeshKind viewport_mesh_kind_from(SceneMeshKind mesh_kind) noexcept
{
    switch (mesh_kind) {
    case SceneMeshKind::cube:
        return ViewportMeshKind::cube;
    case SceneMeshKind::plane:
        return ViewportMeshKind::plane;
    }

    return ViewportMeshKind::cube;
}

} // namespace

SceneRenderSnapshot build_scene_render_snapshot(const Scene& scene)
{
    SceneRenderSnapshot snapshot;
    snapshot.environment = scene.environment_settings();
    snapshot.post_processing = scene.post_processing_settings();
    snapshot.revision = scene.render_revision();

    for (const SceneEntity& entity : scene.entities()) {
        const RenderEntityId render_entity_id { entity.id.value };
        if (entity.mesh.has_value()) {
            snapshot.meshes.push_back(SceneRenderMesh {
                .entity_id = render_entity_id,
                .mesh_kind = viewport_mesh_kind_from(entity.mesh->mesh_kind),
                .position = entity.transform.position,
                .rotation_degrees = entity.transform.get_rotation_degrees(),
                .scale = entity.transform.scale,
                .material = entity.material.has_value() ? entity.material->material : PbrMaterialSettings {},
                .casts_shadows = entity.mesh->casts_shadows,
                .receives_shadows = entity.mesh->receives_shadows,
            });
        }

        if (entity.directional_light.has_value()) {
            snapshot.directional_lights.push_back(SceneRenderDirectionalLight {
                .entity_id = render_entity_id,
                .position = entity.transform.position,
                .direction = rotate_direction(entity.transform.rotation, Vec3 { 0.0f, 0.0f, -1.0f }),
                .right = rotate_direction(entity.transform.rotation, Vec3 { 1.0f, 0.0f, 0.0f }),
                .up = rotate_direction(entity.transform.rotation, Vec3 { 0.0f, 1.0f, 0.0f }),
                .settings = entity.directional_light->settings,
            });
        }

        if (entity.point_light.has_value()) {
            snapshot.point_lights.push_back(SceneRenderPointLight {
                .entity_id = render_entity_id,
                .position = entity.transform.position,
                .settings = entity.point_light->settings,
            });
        }
    }

    return snapshot;
}

} // namespace quader
