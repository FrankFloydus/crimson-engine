module;

#include <algorithm>
#include <cstddef>
#include <cmath>

module quader.render.viewport_gizmos;

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

[[nodiscard]] LinearRgba light_gizmo_color(LinearColor color)
{
    return LinearRgba {
        .r = std::clamp(color.r, 0.0f, 1.0f),
        .g = std::clamp(color.g, 0.0f, 1.0f),
        .b = std::clamp(color.b, 0.0f, 1.0f),
        .a = 1.0f,
    };
}

void append_directional_light_gizmo(
    const SceneRenderDirectionalLight& light,
    DebugDrawList& draw,
    LightGizmoStyle style)
{
    const DebugDrawPickPayload pick = directional_light_gizmo_pick_payload(light.entity_id);
    const Vec3 direction = normalized_or(light.direction, Vec3 { 0.0f, 0.0f, -1.0f });
    const Vec3 start = light.position;
    const Vec3 tip = add(start, scale(direction, style.directional_arrow_length));
    const Vec3 head_base = add(tip, scale(direction, -style.directional_arrow_head_length));
    const Vec3 right = normalized_or(light.right, Vec3 { 1.0f, 0.0f, 0.0f });
    const Vec3 up = normalized_or(light.up, normalized_or(cross(right, direction), Vec3 { 0.0f, 1.0f, 0.0f }));
    const LinearRgba color = light_gizmo_color(light.settings.color);

    draw.add_line(start, tip, color, pick);
    draw.add_line(tip, add(head_base, scale(right, style.directional_arrow_head_width)), color, pick);
    draw.add_line(tip, add(head_base, scale(right, -style.directional_arrow_head_width)), color, pick);
    draw.add_line(tip, add(head_base, scale(up, style.directional_arrow_head_width)), color, pick);
    draw.add_line(tip, add(head_base, scale(up, -style.directional_arrow_head_width)), color, pick);
    draw.add_pick_capsule(pick, start, tip, style.directional_pick_radius, 20);
    draw.add_pick_sphere(pick, start, style.point_pick_radius, 20);
}

void append_circle(
    DebugDrawList& draw,
    Vec3 center,
    Vec3 axis_a,
    Vec3 axis_b,
    float radius,
    uint32_t segments,
    LinearRgba color,
    DebugDrawPickPayload pick)
{
    constexpr float pi = 3.14159265358979323846f;
    const uint32_t safe_segments = std::max<uint32_t>(segments, 8);
    const float angle_step = (pi * 2.0f) / static_cast<float>(safe_segments);

    for (uint32_t segment = 0; segment < safe_segments; ++segment) {
        const float angle_a = static_cast<float>(segment) * angle_step;
        const float angle_b = static_cast<float>(segment + 1) * angle_step;
        const Vec3 point_a = add(
            center,
            add(
                scale(axis_a, std::cos(angle_a) * radius),
                scale(axis_b, std::sin(angle_a) * radius)));
        const Vec3 point_b = add(
            center,
            add(
                scale(axis_a, std::cos(angle_b) * radius),
                scale(axis_b, std::sin(angle_b) * radius)));
        draw.add_line(point_a, point_b, color, pick);
    }
}

void append_point_light_gizmo(
    const SceneRenderPointLight& light,
    DebugDrawList& draw,
    LightGizmoStyle style)
{
    const DebugDrawPickPayload pick = point_light_gizmo_pick_payload(light.entity_id);
    const LinearRgba color = light_gizmo_color(light.settings.color);
    const Vec3 x_axis { 1.0f, 0.0f, 0.0f };
    const Vec3 y_axis { 0.0f, 1.0f, 0.0f };
    const Vec3 z_axis { 0.0f, 0.0f, 1.0f };

    append_circle(draw, light.position, x_axis, y_axis, style.point_radius, style.point_ring_segments, color, pick);
    append_circle(draw, light.position, x_axis, z_axis, style.point_radius, style.point_ring_segments, color, pick);
    append_circle(draw, light.position, y_axis, z_axis, style.point_radius, style.point_ring_segments, color, pick);
    draw.add_pick_sphere(pick, light.position, style.point_pick_radius, 20);
}

} // namespace

DebugDrawPickPayload directional_light_gizmo_pick_payload(RenderEntityId entity_id) noexcept
{
    return DebugDrawPickPayload {
        .owner_entity_id = entity_id.value,
        .role = DebugDrawPickRole::directional_light_gizmo,
    };
}

DebugDrawPickPayload point_light_gizmo_pick_payload(RenderEntityId entity_id) noexcept
{
    return DebugDrawPickPayload {
        .owner_entity_id = entity_id.value,
        .role = DebugDrawPickRole::point_light_gizmo,
    };
}

LightGizmoStyle default_light_gizmo_style() noexcept
{
    return {};
}

void append_light_gizmos(
    const SceneRenderSnapshot& snapshot,
    DebugDrawFrame& debug_draw,
    LightGizmoStyle style)
{
    DebugDrawList& draw = debug_draw.depth_tested;
    draw.reserve_lines(
        draw.lines().size() +
        snapshot.directional_lights.size() * 5 +
        snapshot.point_lights.size() * static_cast<size_t>(std::max<uint32_t>(style.point_ring_segments, 8) * 3));
    draw.reserve_pick_proxies(
        draw.pick_proxies().size() +
        snapshot.directional_lights.size() * 2 +
        snapshot.point_lights.size());

    for (const SceneRenderDirectionalLight& light : snapshot.directional_lights) {
        if (light.entity_id.is_valid()) {
            append_directional_light_gizmo(light, draw, style);
        }
    }

    for (const SceneRenderPointLight& light : snapshot.point_lights) {
        if (light.entity_id.is_valid()) {
            append_point_light_gizmo(light, draw, style);
        }
    }
}

} // namespace quader
