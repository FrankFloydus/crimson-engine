module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <glm/common.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/quaternion_trigonometric.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

module quader.scene.scene;

namespace quader {
namespace {

struct SceneEntityRecord {
    SceneEntityId id;
    std::string name;

    SceneEntityRecord(SceneEntityId entity_id, std::string entity_name)
        : id(entity_id)
        , name(std::move(entity_name))
    {
    }
};

[[nodiscard]] glm::quat to_glm(Quaternion rotation)
{
    return glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
}

[[nodiscard]] Quaternion from_glm(glm::quat rotation)
{
    const glm::quat normalized = glm::normalize(rotation);
    return {
        .x = normalized.x,
        .y = normalized.y,
        .z = normalized.z,
        .w = normalized.w,
    };
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

} // namespace

struct Scene::SceneStorage {
    entt::registry registry;
    std::unordered_map<uint32_t, entt::entity> entity_by_id;
    std::vector<SceneEntityId> entity_order;
    uint32_t next_entity_id = 1;
};

void TransformComponent::set_rotation_degrees(Vec3 degrees)
{
    rotation_degrees = degrees;
    const glm::vec3 radians {
        glm::radians(degrees.x),
        glm::radians(degrees.y),
        glm::radians(degrees.z),
    };
    rotation = from_glm(glm::quat(radians));
}

Vec3 TransformComponent::get_rotation_degrees() const
{
    const glm::vec3 euler = glm::degrees(glm::eulerAngles(to_glm(rotation)));
    return { euler.x, euler.y, euler.z };
}

Scene::Scene()
    : Scene("Untitled Scene")
{
}

Scene::Scene(std::string title)
    : title_(std::move(title))
    , storage_(std::make_unique<SceneStorage>())
{
}

Scene::~Scene() = default;
Scene::Scene(Scene&&) noexcept = default;
Scene& Scene::operator=(Scene&&) noexcept = default;

std::string_view Scene::title() const
{
    return title_;
}

void Scene::set_title(std::string title)
{
    title_ = std::move(title);
}

SceneEntityId Scene::create_entity(std::string name, TransformComponent transform)
{
    return create_named_entity(std::move(name), transform);
}

SceneEntityId Scene::create_cube(std::string name, TransformComponent transform)
{
    const SceneEntityId id = create_named_entity(std::move(name), transform);
    if (const auto found = storage_->entity_by_id.find(id.value); found != storage_->entity_by_id.end()) {
        MeshComponent& component = storage_->registry.emplace<MeshComponent>(found->second);
        component.mesh_kind = SceneMeshKind::cube;
        storage_->registry.emplace<MaterialComponent>(found->second);
    }
    return id;
}

SceneEntityId Scene::create_plane(std::string name, TransformComponent transform)
{
    const SceneEntityId id = create_named_entity(std::move(name), transform);
    if (const auto found = storage_->entity_by_id.find(id.value); found != storage_->entity_by_id.end()) {
        MeshComponent& component = storage_->registry.emplace<MeshComponent>(found->second);
        component.mesh_kind = SceneMeshKind::plane;
        storage_->registry.emplace<MaterialComponent>(found->second);
    }
    return id;
}

SceneEntityId Scene::create_directional_light(std::string name, TransformComponent transform)
{
    const SceneEntityId id = create_named_entity(std::move(name), transform);
    if (const auto found = storage_->entity_by_id.find(id.value); found != storage_->entity_by_id.end()) {
        storage_->registry.emplace<DirectionalLightComponent>(found->second);
    }
    return id;
}

SceneEntityId Scene::create_point_light(std::string name, TransformComponent transform)
{
    const SceneEntityId id = create_named_entity(std::move(name), transform);
    if (const auto found = storage_->entity_by_id.find(id.value); found != storage_->entity_by_id.end()) {
        storage_->registry.emplace<PointLightComponent>(found->second);
    }
    return id;
}

SceneEntity& Scene::add_entity(std::string name, TransformComponent transform)
{
    const SceneEntityId id = create_named_entity(std::move(name), transform);
    rebuild_entity_cache();
    entity_cache_borrowed_ = true;

    const auto found = std::ranges::find_if(entity_cache_, [id](const SceneEntity& entity) {
        return entity.id == id;
    });
    return *found;
}

bool Scene::destroy_entity(SceneEntityId id)
{
    flush_entity_cache();

    const auto found = storage_->entity_by_id.find(id.value);
    if (found == storage_->entity_by_id.end()) {
        return false;
    }

    storage_->registry.destroy(found->second);
    storage_->entity_by_id.erase(found);
    std::erase(storage_->entity_order, id);
    return true;
}

bool Scene::contains(SceneEntityId id) const
{
    flush_entity_cache();
    return storage_->entity_by_id.contains(id.value);
}

std::size_t Scene::entity_count() const
{
    flush_entity_cache();
    return storage_->entity_order.size();
}

std::span<SceneEntity> Scene::entities()
{
    rebuild_entity_cache();
    entity_cache_borrowed_ = true;
    return entity_cache_;
}

std::span<const SceneEntity> Scene::entities() const
{
    rebuild_entity_cache();
    return entity_cache_;
}

std::optional<SceneEntity> Scene::entity(SceneEntityId id) const
{
    rebuild_entity_cache();
    const auto found = std::ranges::find_if(entity_cache_, [id](const SceneEntity& entity) {
        return entity.id == id;
    });
    if (found == entity_cache_.end()) {
        return std::nullopt;
    }

    return *found;
}

TransformComponent* Scene::transform(SceneEntityId id)
{
    flush_entity_cache();

    const auto found = storage_->entity_by_id.find(id.value);
    if (found == storage_->entity_by_id.end()) {
        return nullptr;
    }

    return storage_->registry.try_get<TransformComponent>(found->second);
}

const TransformComponent* Scene::transform(SceneEntityId id) const
{
    return const_cast<Scene*>(this)->transform(id);
}

MeshComponent* Scene::mesh(SceneEntityId id)
{
    flush_entity_cache();

    const auto found = storage_->entity_by_id.find(id.value);
    if (found == storage_->entity_by_id.end()) {
        return nullptr;
    }

    return storage_->registry.try_get<MeshComponent>(found->second);
}

const MeshComponent* Scene::mesh(SceneEntityId id) const
{
    return const_cast<Scene*>(this)->mesh(id);
}

MaterialComponent* Scene::material(SceneEntityId id)
{
    flush_entity_cache();

    const auto found = storage_->entity_by_id.find(id.value);
    if (found == storage_->entity_by_id.end()) {
        return nullptr;
    }

    return storage_->registry.try_get<MaterialComponent>(found->second);
}

const MaterialComponent* Scene::material(SceneEntityId id) const
{
    return const_cast<Scene*>(this)->material(id);
}

DirectionalLightComponent* Scene::directional_light(SceneEntityId id)
{
    flush_entity_cache();

    const auto found = storage_->entity_by_id.find(id.value);
    if (found == storage_->entity_by_id.end()) {
        return nullptr;
    }

    return storage_->registry.try_get<DirectionalLightComponent>(found->second);
}

const DirectionalLightComponent* Scene::directional_light(SceneEntityId id) const
{
    return const_cast<Scene*>(this)->directional_light(id);
}

PointLightComponent* Scene::point_light(SceneEntityId id)
{
    flush_entity_cache();

    const auto found = storage_->entity_by_id.find(id.value);
    if (found == storage_->entity_by_id.end()) {
        return nullptr;
    }

    return storage_->registry.try_get<PointLightComponent>(found->second);
}

const PointLightComponent* Scene::point_light(SceneEntityId id) const
{
    return const_cast<Scene*>(this)->point_light(id);
}

SceneEntityId Scene::create_named_entity(std::string name, TransformComponent transform)
{
    flush_entity_cache();

    const SceneEntityId id { storage_->next_entity_id++ };
    const entt::entity entity = storage_->registry.create();

    storage_->registry.emplace<SceneEntityRecord>(entity, id, std::move(name));
    storage_->registry.emplace<TransformComponent>(entity, transform);
    storage_->entity_by_id.emplace(id.value, entity);
    storage_->entity_order.push_back(id);
    return id;
}

void Scene::flush_entity_cache() const
{
    if (!entity_cache_borrowed_ || flushing_entity_cache_) {
        return;
    }

    flushing_entity_cache_ = true;

    for (const SceneEntity& view : entity_cache_) {
        const auto found = storage_->entity_by_id.find(view.id.value);
        if (found == storage_->entity_by_id.end()) {
            continue;
        }

        const entt::entity entity = found->second;
        if (SceneEntityRecord* record = storage_->registry.try_get<SceneEntityRecord>(entity)) {
            record->name = view.name;
        }

        TransformComponent transform = view.transform;
        transform.set_rotation_degrees(view.transform.rotation_degrees());
        storage_->registry.emplace_or_replace<TransformComponent>(entity, transform);

        if (view.mesh.has_value()) {
            MeshComponent mesh_component = *view.mesh;
            storage_->registry.emplace_or_replace<MeshComponent>(entity, mesh_component);
        } else if (storage_->registry.all_of<MeshComponent>(entity)) {
            storage_->registry.remove<MeshComponent>(entity);
        }

        if (view.material.has_value()) {
            storage_->registry.emplace_or_replace<MaterialComponent>(entity, *view.material);
        } else if (storage_->registry.all_of<MaterialComponent>(entity)) {
            storage_->registry.remove<MaterialComponent>(entity);
        }

        if (view.directional_light.has_value()) {
            storage_->registry.emplace_or_replace<DirectionalLightComponent>(entity, *view.directional_light);
        } else if (storage_->registry.all_of<DirectionalLightComponent>(entity)) {
            storage_->registry.remove<DirectionalLightComponent>(entity);
        }

        if (view.point_light.has_value()) {
            storage_->registry.emplace_or_replace<PointLightComponent>(entity, *view.point_light);
        } else if (storage_->registry.all_of<PointLightComponent>(entity)) {
            storage_->registry.remove<PointLightComponent>(entity);
        }
    }

    entity_cache_borrowed_ = false;
    flushing_entity_cache_ = false;
}

void Scene::rebuild_entity_cache() const
{
    flush_entity_cache();

    entity_cache_.clear();
    entity_cache_.reserve(storage_->entity_order.size());

    for (SceneEntityId id : storage_->entity_order) {
        const auto found = storage_->entity_by_id.find(id.value);
        if (found == storage_->entity_by_id.end()) {
            continue;
        }

        const entt::entity entity = found->second;
        const SceneEntityRecord* record = storage_->registry.try_get<SceneEntityRecord>(entity);
        const TransformComponent* transform_component = storage_->registry.try_get<TransformComponent>(entity);
        if (record == nullptr || transform_component == nullptr) {
            continue;
        }

        SceneEntity view {
            .id = record->id,
            .name = record->name,
            .transform = *transform_component,
        };

        if (const MeshComponent* mesh_component = storage_->registry.try_get<MeshComponent>(entity)) {
            view.mesh = *mesh_component;
        }

        if (const MaterialComponent* material_component = storage_->registry.try_get<MaterialComponent>(entity)) {
            view.material = *material_component;
        }

        if (const DirectionalLightComponent* light = storage_->registry.try_get<DirectionalLightComponent>(entity)) {
            view.directional_light = *light;
        }

        if (const PointLightComponent* light = storage_->registry.try_get<PointLightComponent>(entity)) {
            view.point_light = *light;
        }

        entity_cache_.push_back(std::move(view));
    }
}

SceneRenderSnapshot build_scene_render_snapshot(const Scene& scene)
{
    SceneRenderSnapshot snapshot;
    snapshot.revision = 1;

    for (const SceneEntity& entity : scene.entities()) {
        const RenderEntityId render_entity_id { entity.id.value };
        if (entity.mesh.has_value()) {
            ViewportMeshKind mesh_kind = ViewportMeshKind::cube;
            switch (entity.mesh->mesh_kind) {
            case SceneMeshKind::cube:
                mesh_kind = ViewportMeshKind::cube;
                break;
            case SceneMeshKind::plane:
                mesh_kind = ViewportMeshKind::plane;
                break;
            }

            snapshot.meshes.push_back(SceneRenderMesh {
                .entity_id = render_entity_id,
                .mesh_kind = mesh_kind,
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
