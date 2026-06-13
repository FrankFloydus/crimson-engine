module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

module quader.app.editor_viewport_picking;

import quader.render.debug_draw_picking;
import quader.render.viewport_gizmos;

namespace quader {
namespace {

struct SceneRayHit {
    SceneEntityId entity_id;
    std::optional<DebugDrawPickPayload> debug_payload;
    float distance = 0.0f;
    uint8_t priority = 0;
};

float degrees_to_radians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

[[nodiscard]] Vec3 subtract(Vec3 left, Vec3 right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

[[nodiscard]] Vec3 scale(Vec3 value, float amount)
{
    return { value.x * amount, value.y * amount, value.z * amount };
}

[[nodiscard]] Vec3 multiply(Vec3 left, Vec3 right)
{
    return { left.x * right.x, left.y * right.y, left.z * right.z };
}

[[nodiscard]] Vec3 divide(Vec3 left, Vec3 right)
{
    return {
        right.x != 0.0f ? left.x / right.x : left.x,
        right.y != 0.0f ? left.y / right.y : left.y,
        right.z != 0.0f ? left.z / right.z : left.z,
    };
}

[[nodiscard]] float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float squared_length(Vec3 value)
{
    return dot(value, value);
}

[[nodiscard]] Vec3 cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vec3 normalized_or(Vec3 value, Vec3 fallback)
{
    const float length_squared = squared_length(value);
    if (length_squared <= 0.000001f) {
        return fallback;
    }

    return scale(value, 1.0f / std::sqrt(length_squared));
}

[[nodiscard]] Vec3 rotate_by_quaternion(Vec3 value, Quaternion rotation)
{
    const Vec3 u { rotation.x, rotation.y, rotation.z };
    const float s = rotation.w;
    return add(
        add(scale(u, 2.0f * dot(u, value)), scale(value, s * s - dot(u, u))),
        scale(cross(u, value), 2.0f * s));
}

[[nodiscard]] Vec3 inverse_rotate_by_quaternion(Vec3 value, Quaternion rotation)
{
    rotation.x = -rotation.x;
    rotation.y = -rotation.y;
    rotation.z = -rotation.z;
    return rotate_by_quaternion(value, rotation);
}

[[nodiscard]] std::optional<float> intersect_ray_aabb(
    const DebugDrawPickRay& ray,
    Vec3 bounds_min,
    Vec3 bounds_max)
{
    float t_min = 0.0f;
    float t_max = std::numeric_limits<float>::max();

    const float origins[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
    const float directions[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
    const float mins[3] = { bounds_min.x, bounds_min.y, bounds_min.z };
    const float maxes[3] = { bounds_max.x, bounds_max.y, bounds_max.z };

    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(directions[axis]) <= 0.000001f) {
            if (origins[axis] < mins[axis] || origins[axis] > maxes[axis]) {
                return std::nullopt;
            }
            continue;
        }

        float near_t = (mins[axis] - origins[axis]) / directions[axis];
        float far_t = (maxes[axis] - origins[axis]) / directions[axis];
        if (near_t > far_t) {
            std::swap(near_t, far_t);
        }
        t_min = std::max(t_min, near_t);
        t_max = std::min(t_max, far_t);
        if (t_min > t_max) {
            return std::nullopt;
        }
    }

    return t_min;
}

[[nodiscard]] DebugDrawPickRay viewport_ray_from_surface_pointer(
    ViewportCamera camera,
    ViewportProjection projection,
    const ViewportSurfaceResult& surface,
    ViewportSurfacePointer pointer)
{
    const float aspect =
        surface.image_bounds.height > 0.0f ? surface.image_bounds.width / surface.image_bounds.height : 1.0f;
    const float normalized_x = surface.image_bounds.width > 0.0f
        ? (pointer.local_x / surface.image_bounds.width) * 2.0f - 1.0f
        : 0.0f;
    const float normalized_y = surface.image_bounds.height > 0.0f
        ? 1.0f - (pointer.local_y / surface.image_bounds.height) * 2.0f
        : 0.0f;

    const Vec3 forward = normalized_or(camera.forward, { 0.0f, 0.0f, -1.0f });
    const Vec3 right = normalized_or(cross(forward, camera.up), { 1.0f, 0.0f, 0.0f });
    const Vec3 up = normalized_or(cross(right, forward), { 0.0f, 1.0f, 0.0f });

    if (projection.kind == ViewportProjectionKind::orthographic) {
        const float half_height = projection.orthographic_size * 0.5f;
        const float half_width = half_height * aspect;
        return DebugDrawPickRay {
            .origin = add(
                camera.position,
                add(scale(right, normalized_x * half_width), scale(up, normalized_y * half_height))),
            .direction = forward,
        };
    }

    const float half_fov_radians = degrees_to_radians(projection.vertical_fov_degrees) * 0.5f;
    const float half_height = std::tan(half_fov_radians);
    const float half_width = half_height * aspect;
    return DebugDrawPickRay {
        .origin = camera.position,
        .direction = normalized_or(
            add(forward, add(scale(right, normalized_x * half_width), scale(up, normalized_y * half_height))),
            forward),
    };
}

[[nodiscard]] std::optional<float> intersect_mesh_entity(const DebugDrawPickRay& ray, const SceneEntity& entity)
{
    if (!entity.mesh.has_value()) {
        return std::nullopt;
    }

    const Vec3 safe_scale {
        std::abs(entity.transform.scale.x) > 0.000001f ? entity.transform.scale.x : 1.0f,
        std::abs(entity.transform.scale.y) > 0.000001f ? entity.transform.scale.y : 1.0f,
        std::abs(entity.transform.scale.z) > 0.000001f ? entity.transform.scale.z : 1.0f,
    };
    const DebugDrawPickRay local_ray {
        .origin = divide(
            inverse_rotate_by_quaternion(subtract(ray.origin, entity.transform.position), entity.transform.rotation),
            safe_scale),
        .direction = divide(inverse_rotate_by_quaternion(ray.direction, entity.transform.rotation), safe_scale),
    };

    const bool is_plane = entity.mesh->mesh_kind == SceneMeshKind::plane;
    const Vec3 bounds_min = is_plane ? Vec3 { -0.5f, -0.03f, -0.5f } : Vec3 { -0.5f, -0.5f, -0.5f };
    const Vec3 bounds_max = is_plane ? Vec3 { 0.5f, 0.03f, 0.5f } : Vec3 { 0.5f, 0.5f, 0.5f };
    const std::optional<float> local_t = intersect_ray_aabb(local_ray, bounds_min, bounds_max);
    if (!local_t.has_value()) {
        return std::nullopt;
    }

    const Vec3 local_hit = add(local_ray.origin, scale(local_ray.direction, *local_t));
    const Vec3 world_hit = add(
        entity.transform.position,
        rotate_by_quaternion(multiply(local_hit, safe_scale), entity.transform.rotation));
    return std::sqrt(squared_length(subtract(world_hit, ray.origin)));
}

[[nodiscard]] bool first_hit_is_better(const SceneRayHit& candidate, const std::optional<SceneRayHit>& current)
{
    if (!current.has_value()) {
        return true;
    }

    if (candidate.priority != current->priority) {
        return candidate.priority > current->priority;
    }

    return candidate.distance < current->distance;
}

} // namespace

EditorViewportPickResult pick_editor_viewport_surface(
    const Scene& scene,
    ViewportCamera camera,
    ViewportProjection projection,
    const ViewportSurfaceResult& surface,
    const std::optional<TransformGizmoRequest>& transform_gizmo)
{
    std::optional<ViewportSurfacePointer> pointer = surface.pointer;
    if (!pointer.has_value() && surface.click.has_value()) {
        pointer = ViewportSurfacePointer {
            .local_x = surface.click->local_x,
            .local_y = surface.click->local_y,
        };
    }

    if (!pointer.has_value() || surface.image_bounds.width <= 0.0f || surface.image_bounds.height <= 0.0f) {
        return {};
    }

    const DebugDrawPickRay ray = viewport_ray_from_surface_pointer(camera, projection, surface, *pointer);
    std::optional<SceneRayHit> best_hit;

    SceneRenderSnapshot snapshot = build_scene_render_snapshot(scene);
    DebugDrawFrame debug_draw;
    append_light_gizmos(snapshot, debug_draw);
    if (transform_gizmo.has_value()) {
        append_transform_gizmo(*transform_gizmo, debug_draw);
    }

    const std::optional<DebugDrawPickHit> debug_hit = pick_debug_draw_frame(debug_draw, ray);
    if (debug_hit.has_value()) {
        const SceneEntityId owner { debug_hit->payload.owner_entity_id };
        if (scene.contains(owner)) {
            best_hit = SceneRayHit {
                .entity_id = owner,
                .debug_payload = debug_hit->payload,
                .distance = debug_hit->distance,
                .priority = debug_hit->priority,
            };
        }
    }

    for (const SceneEntity& entity : scene.entities()) {
        const std::optional<float> hit_distance = intersect_mesh_entity(ray, entity);
        if (!hit_distance.has_value()) {
            continue;
        }

        SceneRayHit candidate {
            .entity_id = entity.id,
            .distance = *hit_distance,
        };
        if (first_hit_is_better(candidate, best_hit)) {
            best_hit = candidate;
        }
    }

    if (!best_hit.has_value()) {
        return {};
    }

    return EditorViewportPickResult {
        .entity_id = best_hit->entity_id,
        .debug_payload = best_hit->debug_payload,
    };
}

} // namespace quader
