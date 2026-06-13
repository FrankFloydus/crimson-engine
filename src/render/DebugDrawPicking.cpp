module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

module quader.render.debug_draw_picking;

namespace quader {
namespace {

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

[[nodiscard]] float dot(Vec3 left, Vec3 right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] float squared_length(Vec3 value)
{
    return dot(value, value);
}

[[nodiscard]] Vec3 normalized_or(Vec3 value, Vec3 fallback)
{
    const float length_squared = squared_length(value);
    if (length_squared <= 0.000001f) {
        return fallback;
    }

    return scale(value, 1.0f / std::sqrt(length_squared));
}

[[nodiscard]] std::optional<float> intersect_ray_sphere(
    const DebugDrawPickRay& ray,
    Vec3 center,
    float radius)
{
    const Vec3 offset = subtract(ray.origin, center);
    const float a = dot(ray.direction, ray.direction);
    const float b = 2.0f * dot(offset, ray.direction);
    const float c = dot(offset, offset) - radius * radius;
    const float discriminant = b * b - 4.0f * a * c;
    if (a <= 0.0f || discriminant < 0.0f) {
        return std::nullopt;
    }

    const float root = std::sqrt(discriminant);
    const float near_t = (-b - root) / (2.0f * a);
    const float far_t = (-b + root) / (2.0f * a);
    if (near_t >= 0.0f) {
        return near_t;
    }
    if (far_t >= 0.0f) {
        return far_t;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<float> intersect_ray_capsule(
    const DebugDrawPickRay& ray,
    Vec3 start,
    Vec3 end,
    float radius)
{
    if (radius <= 0.0f) {
        return std::nullopt;
    }

    const Vec3 segment = subtract(end, start);
    const float segment_length_squared = squared_length(segment);
    if (segment_length_squared <= 0.000001f) {
        return intersect_ray_sphere(ray, start, radius);
    }

    const float ray_length_squared = squared_length(ray.direction);
    if (ray_length_squared <= 0.000001f) {
        return std::nullopt;
    }

    float ray_t = 0.0f;
    float segment_t = 0.0f;
    const Vec3 origin_to_start = subtract(ray.origin, start);
    const float ray_segment_dot = dot(ray.direction, segment);
    const float origin_ray_dot = dot(origin_to_start, ray.direction);
    const float origin_segment_dot = dot(origin_to_start, segment);
    const float denominator = ray_length_squared * segment_length_squared - ray_segment_dot * ray_segment_dot;

    if (std::abs(denominator) > 0.000001f) {
        ray_t = (ray_segment_dot * origin_segment_dot - segment_length_squared * origin_ray_dot) / denominator;
        segment_t = (ray_length_squared * origin_segment_dot - ray_segment_dot * origin_ray_dot) / denominator;
    }

    segment_t = std::clamp(segment_t, 0.0f, 1.0f);
    const Vec3 segment_point = add(start, scale(segment, segment_t));
    ray_t = dot(subtract(segment_point, ray.origin), ray.direction) / ray_length_squared;
    ray_t = std::max(ray_t, 0.0f);

    const Vec3 ray_point = add(ray.origin, scale(ray.direction, ray_t));
    segment_t = std::clamp(dot(subtract(ray_point, start), segment) / segment_length_squared, 0.0f, 1.0f);
    const Vec3 closest_segment_point = add(start, scale(segment, segment_t));
    ray_t = dot(subtract(closest_segment_point, ray.origin), ray.direction) / ray_length_squared;
    ray_t = std::max(ray_t, 0.0f);
    const Vec3 closest_ray_point = add(ray.origin, scale(ray.direction, ray_t));

    if (squared_length(subtract(closest_ray_point, closest_segment_point)) > radius * radius) {
        return std::nullopt;
    }

    return ray_t;
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

[[nodiscard]] std::optional<float> intersect_ray_oriented_box(
    const DebugDrawPickRay& ray,
    Vec3 center,
    Vec3 axis_x,
    Vec3 axis_y,
    Vec3 axis_z,
    Vec3 half_extents)
{
    axis_x = normalized_or(axis_x, { 1.0f, 0.0f, 0.0f });
    axis_y = normalized_or(axis_y, { 0.0f, 1.0f, 0.0f });
    axis_z = normalized_or(axis_z, { 0.0f, 0.0f, 1.0f });
    const Vec3 origin_delta = subtract(ray.origin, center);
    const DebugDrawPickRay local_ray {
        .origin = {
            dot(origin_delta, axis_x),
            dot(origin_delta, axis_y),
            dot(origin_delta, axis_z),
        },
        .direction = {
            dot(ray.direction, axis_x),
            dot(ray.direction, axis_y),
            dot(ray.direction, axis_z),
        },
    };
    return intersect_ray_aabb(
        local_ray,
        scale(half_extents, -1.0f),
        half_extents);
}

[[nodiscard]] std::optional<float> intersect_ray_ring(
    const DebugDrawPickRay& ray,
    Vec3 center,
    Vec3 normal,
    float radius,
    float thickness)
{
    if (radius <= 0.0f || thickness <= 0.0f) {
        return std::nullopt;
    }

    normal = normalized_or(normal, { 0.0f, 1.0f, 0.0f });
    const float denominator = dot(ray.direction, normal);
    if (std::abs(denominator) <= 0.000001f) {
        return std::nullopt;
    }

    const float t = dot(subtract(center, ray.origin), normal) / denominator;
    if (t < 0.0f) {
        return std::nullopt;
    }

    const Vec3 hit = add(ray.origin, scale(ray.direction, t));
    const Vec3 center_to_hit = subtract(hit, center);
    const Vec3 radial = subtract(center_to_hit, scale(normal, dot(center_to_hit, normal)));
    if (std::abs(std::sqrt(squared_length(radial)) - radius) > thickness) {
        return std::nullopt;
    }

    return t;
}

[[nodiscard]] bool hit_is_better(
    const DebugDrawPickHit& candidate,
    const std::optional<DebugDrawPickHit>& current)
{
    if (!current.has_value()) {
        return true;
    }

    if (candidate.priority != current->priority) {
        return candidate.priority > current->priority;
    }

    return candidate.distance < current->distance;
}

[[nodiscard]] std::optional<float> intersect_pick_proxy(
    const DebugDrawPickProxy& proxy,
    const DebugDrawPickRay& ray)
{
    switch (proxy.kind) {
    case DebugDrawPickProxyKind::sphere:
        return intersect_ray_sphere(ray, proxy.center, proxy.radius);
    case DebugDrawPickProxyKind::capsule:
        return intersect_ray_capsule(ray, proxy.segment_a, proxy.segment_b, proxy.radius);
    case DebugDrawPickProxyKind::oriented_box:
        return intersect_ray_oriented_box(
            ray,
            proxy.center,
            proxy.axis_x,
            proxy.axis_y,
            proxy.axis_z,
            proxy.half_extents);
    case DebugDrawPickProxyKind::ring:
        return intersect_ray_ring(ray, proxy.center, proxy.normal, proxy.radius, proxy.thickness);
    }

    return std::nullopt;
}

} // namespace

std::optional<DebugDrawPickHit> pick_debug_draw_list(
    const DebugDrawList& draw,
    const DebugDrawPickRay& ray)
{
    std::optional<DebugDrawPickHit> best_hit;
    for (const DebugDrawPickProxy& proxy : draw.pick_proxies()) {
        if (!proxy.is_valid()) {
            continue;
        }

        const std::optional<float> hit_distance = intersect_pick_proxy(proxy, ray);
        if (!hit_distance.has_value()) {
            continue;
        }

        DebugDrawPickHit candidate {
            .payload = proxy.payload,
            .distance = *hit_distance,
            .priority = proxy.priority,
        };
        if (hit_is_better(candidate, best_hit)) {
            best_hit = candidate;
        }
    }

    return best_hit;
}

std::optional<DebugDrawPickHit> pick_debug_draw_frame(
    const DebugDrawFrame& frame,
    const DebugDrawPickRay& ray)
{
    std::optional<DebugDrawPickHit> best_hit = pick_debug_draw_list(frame.depth_tested, ray);
    const std::optional<DebugDrawPickHit> overlay_hit = pick_debug_draw_list(frame.overlay, ray);
    if (overlay_hit.has_value() && hit_is_better(*overlay_hit, best_hit)) {
        best_hit = overlay_hit;
    }

    return best_hit;
}

} // namespace quader
