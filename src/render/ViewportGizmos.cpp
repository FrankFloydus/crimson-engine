module;

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>

module quader.render.viewport_gizmos;

namespace quader {
namespace {

constexpr float pi = 3.14159265358979323846f;

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

[[nodiscard]] Vec3 axis_vector(DebugDrawAxis axis)
{
    switch (axis) {
    case DebugDrawAxis::x:
        return { 1.0f, 0.0f, 0.0f };
    case DebugDrawAxis::y:
        return { 0.0f, 1.0f, 0.0f };
    case DebugDrawAxis::z:
        return { 0.0f, 0.0f, 1.0f };
    case DebugDrawAxis::none:
        break;
    }

    return {};
}

[[nodiscard]] uint8_t raised_pick_priority(uint8_t base_priority, uint8_t amount) noexcept
{
    constexpr int max_priority = 255;
    return static_cast<uint8_t>(
        std::min(max_priority, static_cast<int>(base_priority) + static_cast<int>(amount)));
}

[[nodiscard]] LinearRgba axis_color(DebugDrawAxis axis)
{
    switch (axis) {
    case DebugDrawAxis::x:
        return { 1.0f, 0.02f, 0.08f, 1.0f };
    case DebugDrawAxis::y:
        return { 0.48f, 1.0f, 0.04f, 1.0f };
    case DebugDrawAxis::z:
        return { 0.02f, 0.46f, 1.0f, 1.0f };
    case DebugDrawAxis::none:
        break;
    }

    return { 1.0f, 1.0f, 1.0f, 1.0f };
}

[[nodiscard]] LinearRgba brighten(LinearRgba color, float amount)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    return {
        .r = color.r + (1.0f - color.r) * amount,
        .g = color.g + (1.0f - color.g) * amount,
        .b = color.b + (1.0f - color.b) * amount,
        .a = color.a,
    };
}

[[nodiscard]] bool payload_matches(
    const std::optional<DebugDrawPickPayload>& highlighted,
    DebugDrawPickPayload payload)
{
    return highlighted.has_value() && *highlighted == payload;
}

[[nodiscard]] LinearRgba color_for_payload(
    LinearRgba base_color,
    const std::optional<DebugDrawPickPayload>& highlighted,
    DebugDrawPickPayload payload)
{
    if (payload_matches(highlighted, payload)) {
        return brighten(base_color, 0.68f);
    }

    return base_color;
}

[[nodiscard]] Vec3 perpendicular_to(Vec3 direction)
{
    const Vec3 up { 0.0f, 1.0f, 0.0f };
    const Vec3 right { 1.0f, 0.0f, 0.0f };
    Vec3 perpendicular = cross(direction, up);
    if (squared_length(perpendicular) <= 0.000001f) {
        perpendicular = cross(direction, right);
    }
    return normalized_or(perpendicular, right);
}

[[nodiscard]] Vec3 target_axis_vector(const TransformGizmoTarget& target, DebugDrawAxis axis)
{
    switch (axis) {
    case DebugDrawAxis::x:
        return normalized_or(target.x_axis, Vec3 { 1.0f, 0.0f, 0.0f });
    case DebugDrawAxis::y:
        return normalized_or(target.y_axis, Vec3 { 0.0f, 1.0f, 0.0f });
    case DebugDrawAxis::z:
        return normalized_or(target.z_axis, Vec3 { 0.0f, 0.0f, 1.0f });
    case DebugDrawAxis::none:
        break;
    }

    return {};
}

void plane_axes_for_normal(
    const TransformGizmoTarget& target,
    DebugDrawAxis normal_axis,
    Vec3& axis_u,
    Vec3& axis_v)
{
    switch (normal_axis) {
    case DebugDrawAxis::x:
        axis_u = target_axis_vector(target, DebugDrawAxis::y);
        axis_v = target_axis_vector(target, DebugDrawAxis::z);
        break;
    case DebugDrawAxis::y:
        axis_u = target_axis_vector(target, DebugDrawAxis::x);
        axis_v = target_axis_vector(target, DebugDrawAxis::z);
        break;
    case DebugDrawAxis::z:
        axis_u = target_axis_vector(target, DebugDrawAxis::x);
        axis_v = target_axis_vector(target, DebugDrawAxis::y);
        break;
    case DebugDrawAxis::none:
        axis_u = { 1.0f, 0.0f, 0.0f };
        axis_v = { 0.0f, 1.0f, 0.0f };
        break;
    }
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

void append_filled_cone(
    DebugDrawList& draw,
    Vec3 base_center,
    Vec3 tip,
    float base_radius,
    uint32_t segments,
    LinearRgba color,
    DebugDrawPickPayload pick)
{
    const Vec3 direction = normalized_or(subtract(tip, base_center), Vec3 { 0.0f, 1.0f, 0.0f });
    const Vec3 axis_a = perpendicular_to(direction);
    const Vec3 axis_b = normalized_or(cross(direction, axis_a), Vec3 { 0.0f, 0.0f, 1.0f });
    const uint32_t safe_segments = std::max<uint32_t>(segments, 8);
    const float angle_step = (pi * 2.0f) / static_cast<float>(safe_segments);

    for (uint32_t segment = 0; segment < safe_segments; ++segment) {
        const float angle_a = static_cast<float>(segment) * angle_step;
        const float angle_b = static_cast<float>(segment + 1) * angle_step;
        const Vec3 point_a = add(
            base_center,
            add(scale(axis_a, std::cos(angle_a) * base_radius), scale(axis_b, std::sin(angle_a) * base_radius)));
        const Vec3 point_b = add(
            base_center,
            add(scale(axis_a, std::cos(angle_b) * base_radius), scale(axis_b, std::sin(angle_b) * base_radius)));

        draw.add_triangle(tip, point_a, point_b, color, pick);
        draw.add_triangle(base_center, point_b, point_a, color, pick);
    }
}

void append_filled_box(
    DebugDrawList& draw,
    Vec3 center,
    Vec3 axis_x,
    Vec3 axis_y,
    Vec3 axis_z,
    Vec3 half_extents,
    LinearRgba color,
    DebugDrawPickPayload pick)
{
    axis_x = normalized_or(axis_x, Vec3 { 1.0f, 0.0f, 0.0f });
    axis_y = normalized_or(axis_y, Vec3 { 0.0f, 1.0f, 0.0f });
    axis_z = normalized_or(axis_z, Vec3 { 0.0f, 0.0f, 1.0f });

    const Vec3 x = scale(axis_x, half_extents.x);
    const Vec3 y = scale(axis_y, half_extents.y);
    const Vec3 z = scale(axis_z, half_extents.z);
    const Vec3 corners[8] = {
        add(center, add(add(scale(x, -1.0f), scale(y, -1.0f)), scale(z, -1.0f))),
        add(center, add(add(x, scale(y, -1.0f)), scale(z, -1.0f))),
        add(center, add(add(x, y), scale(z, -1.0f))),
        add(center, add(add(scale(x, -1.0f), y), scale(z, -1.0f))),
        add(center, add(add(scale(x, -1.0f), scale(y, -1.0f)), z)),
        add(center, add(add(x, scale(y, -1.0f)), z)),
        add(center, add(add(x, y), z)),
        add(center, add(add(scale(x, -1.0f), y), z)),
    };

    const uint8_t faces[12][3] = {
        { 0, 2, 1 }, { 0, 3, 2 },
        { 4, 5, 6 }, { 4, 6, 7 },
        { 0, 1, 5 }, { 0, 5, 4 },
        { 1, 2, 6 }, { 1, 6, 5 },
        { 2, 3, 7 }, { 2, 7, 6 },
        { 3, 0, 4 }, { 3, 4, 7 },
    };

    for (const auto& face : faces) {
        draw.add_triangle(corners[face[0]], corners[face[1]], corners[face[2]], color, pick);
    }
}

void append_filled_quad(
    DebugDrawList& draw,
    Vec3 a,
    Vec3 b,
    Vec3 c,
    Vec3 d,
    LinearRgba color,
    DebugDrawPickPayload pick)
{
    draw.add_triangle(a, b, c, color, pick);
    draw.add_triangle(a, c, d, color, pick);
}

void append_axis_shaft(
    DebugDrawList& draw,
    Vec3 origin,
    Vec3 direction,
    float length,
    float half_extent,
    LinearRgba color,
    DebugDrawPickPayload pick)
{
    direction = normalized_or(direction, Vec3 { 1.0f, 0.0f, 0.0f });
    const Vec3 axis_y = perpendicular_to(direction);
    const Vec3 axis_z = normalized_or(cross(direction, axis_y), Vec3 { 0.0f, 1.0f, 0.0f });
    append_filled_box(
        draw,
        add(origin, scale(direction, length * 0.5f)),
        direction,
        axis_y,
        axis_z,
        Vec3 { length * 0.5f, half_extent, half_extent },
        color,
        pick);
}

void append_volumetric_ring(
    DebugDrawList& draw,
    Vec3 center,
    Vec3 normal,
    Vec3 tangent,
    float radius,
    float tube_radius,
    uint32_t segments,
    LinearRgba color,
    DebugDrawPickPayload pick)
{
    normal = normalized_or(normal, Vec3 { 0.0f, 1.0f, 0.0f });
    tangent = normalized_or(tangent, perpendicular_to(normal));
    const Vec3 bitangent = normalized_or(cross(normal, tangent), Vec3 { 0.0f, 0.0f, 1.0f });
    const uint32_t safe_segments = std::max<uint32_t>(segments, 16);
    constexpr uint32_t cross_section_segments = 3;
    const float angle_step = (pi * 2.0f) / static_cast<float>(safe_segments);

    const auto ring_vertex = [&](uint32_t segment, uint32_t cross_section) {
        const float angle = static_cast<float>(segment) * angle_step;
        const Vec3 radial = add(scale(tangent, std::cos(angle)), scale(bitangent, std::sin(angle)));
        const Vec3 centerline = add(center, scale(radial, radius));
        const float cross_section_angle =
            (pi * 2.0f * static_cast<float>(cross_section)) / static_cast<float>(cross_section_segments);

        return add(
            centerline,
            add(
                scale(radial, std::cos(cross_section_angle) * tube_radius),
                scale(normal, std::sin(cross_section_angle) * tube_radius)));
    };

    for (uint32_t segment = 0; segment < safe_segments; ++segment) {
        const uint32_t next_segment = (segment + 1U) % safe_segments;
        for (uint32_t cross_section = 0; cross_section < cross_section_segments; ++cross_section) {
            const uint32_t next_cross_section = (cross_section + 1U) % cross_section_segments;

            const Vec3 current_a = ring_vertex(segment, cross_section);
            const Vec3 current_b = ring_vertex(segment, next_cross_section);
            const Vec3 next_a = ring_vertex(next_segment, cross_section);
            const Vec3 next_b = ring_vertex(next_segment, next_cross_section);

            draw.add_triangle(current_b, current_a, next_a, color, pick);
            draw.add_triangle(next_a, next_b, current_b, color, pick);
        }
    }
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

void append_move_axis(
    DebugDrawList& draw,
    const TransformGizmoTarget& target,
    DebugDrawAxis axis,
    TransformGizmoStyle style,
    float world_scale,
    const std::optional<DebugDrawPickPayload>& highlighted)
{
    const Vec3 direction = target_axis_vector(target, axis);
    const DebugDrawPickPayload pick =
        transform_gizmo_pick_payload(target.entity_id, DebugDrawPickRole::transform_move_axis, axis);
    const LinearRgba color = color_for_payload(axis_color(axis), highlighted, pick);
    const float axis_length = style.axis_length * world_scale;
    const float cone_length = style.cone_length * world_scale;
    const Vec3 tip = add(target.origin, scale(direction, axis_length));
    const Vec3 cone_base = add(target.origin, scale(direction, axis_length - cone_length));

    append_axis_shaft(
        draw,
        target.origin,
        direction,
        axis_length - cone_length,
        style.shaft_half_extent * world_scale,
        color,
        pick);
    append_filled_cone(draw, cone_base, tip, style.cone_radius * world_scale, style.cone_segments, color, pick);
    draw.add_pick_capsule(pick, target.origin, tip, style.pick_radius * world_scale, style.pick_priority);
    draw.add_pick_sphere(pick, tip, style.pick_radius * 1.35f * world_scale, style.pick_priority);
}

void append_plane_handle(
    DebugDrawList& draw,
    const TransformGizmoTarget& target,
    DebugDrawAxis normal_axis,
    DebugDrawPickRole role,
    TransformGizmoStyle style,
    float world_scale,
    const std::optional<DebugDrawPickPayload>& highlighted)
{
    Vec3 axis_u;
    Vec3 axis_v;
    plane_axes_for_normal(target, normal_axis, axis_u, axis_v);
    const Vec3 normal = target_axis_vector(target, normal_axis);
    const DebugDrawPickPayload pick = transform_gizmo_pick_payload(target.entity_id, role, normal_axis);
    LinearRgba color = color_for_payload(axis_color(normal_axis), highlighted, pick);
    color.a = payload_matches(highlighted, pick) ? 0.96f : 0.72f;

    const float offset = style.plane_offset * world_scale;
    const float half_size = style.plane_size * 0.5f * world_scale;
    const Vec3 center = add(target.origin, scale(add(axis_u, axis_v), offset));
    const Vec3 u = scale(axis_u, half_size);
    const Vec3 v = scale(axis_v, half_size);

    append_filled_quad(
        draw,
        add(center, add(scale(u, -1.0f), scale(v, -1.0f))),
        add(center, add(u, scale(v, -1.0f))),
        add(center, add(u, v)),
        add(center, add(scale(u, -1.0f), v)),
        color,
        pick);
    draw.add_pick_oriented_box(
        pick,
        center,
        axis_u,
        axis_v,
        normal,
        Vec3 {
            half_size * 1.35f,
            half_size * 1.35f,
            style.plane_pick_thickness * world_scale,
        },
        style.pick_priority);
}

void append_scale_axis(
    DebugDrawList& draw,
    const TransformGizmoTarget& target,
    DebugDrawAxis axis,
    TransformGizmoStyle style,
    float world_scale,
    const std::optional<DebugDrawPickPayload>& highlighted)
{
    const Vec3 direction = target_axis_vector(target, axis);
    const DebugDrawPickPayload pick =
        transform_gizmo_pick_payload(target.entity_id, DebugDrawPickRole::transform_scale_axis, axis);
    const LinearRgba color = color_for_payload(axis_color(axis), highlighted, pick);
    const float axis_length = style.axis_length * world_scale;
    const float cube_half_extent = style.cube_half_extent * world_scale;
    const Vec3 cube_center = add(target.origin, scale(direction, axis_length));
    const Vec3 half_extents { cube_half_extent, cube_half_extent, cube_half_extent };

    append_axis_shaft(
        draw,
        target.origin,
        direction,
        axis_length - cube_half_extent,
        style.shaft_half_extent * world_scale,
        color,
        pick);
    append_filled_box(
        draw,
        cube_center,
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
        half_extents,
        color,
        pick);
    draw.add_pick_capsule(pick, target.origin, cube_center, style.pick_radius * world_scale, style.pick_priority);
    draw.add_pick_oriented_box(
        pick,
        cube_center,
        Vec3 { 1.0f, 0.0f, 0.0f },
        Vec3 { 0.0f, 1.0f, 0.0f },
        Vec3 { 0.0f, 0.0f, 1.0f },
        scale(half_extents, 1.6f),
        style.pick_priority);
}

void append_uniform_scale_handle(
    DebugDrawList& draw,
    const TransformGizmoTarget& target,
    TransformGizmoStyle style,
    float world_scale,
    const std::optional<DebugDrawPickPayload>& highlighted)
{
    const DebugDrawPickPayload pick =
        transform_gizmo_pick_payload(target.entity_id, DebugDrawPickRole::transform_uniform_scale, DebugDrawAxis::none);
    const LinearRgba color = color_for_payload(LinearRgba { 0.92f, 0.96f, 1.0f, 1.0f }, highlighted, pick);
    const float half_extent = style.center_cube_half_extent * world_scale;
    const Vec3 half_extents { half_extent, half_extent, half_extent };
    const uint8_t center_pick_priority = raised_pick_priority(style.pick_priority, 4);

    append_filled_box(
        draw,
        target.origin,
        target_axis_vector(target, DebugDrawAxis::x),
        target_axis_vector(target, DebugDrawAxis::y),
        target_axis_vector(target, DebugDrawAxis::z),
        half_extents,
        color,
        pick);
    draw.add_pick_oriented_box(
        pick,
        target.origin,
        target_axis_vector(target, DebugDrawAxis::x),
        target_axis_vector(target, DebugDrawAxis::y),
        target_axis_vector(target, DebugDrawAxis::z),
        scale(half_extents, 1.75f),
        center_pick_priority);
}

void append_rotate_axis(
    DebugDrawList& draw,
    const TransformGizmoTarget& target,
    DebugDrawAxis axis,
    TransformGizmoStyle style,
    float world_scale,
    const std::optional<DebugDrawPickPayload>& highlighted)
{
    const Vec3 normal = target_axis_vector(target, axis);
    const Vec3 tangent = perpendicular_to(normal);
    const DebugDrawPickPayload pick =
        transform_gizmo_pick_payload(target.entity_id, DebugDrawPickRole::transform_rotate_axis, axis);
    const LinearRgba color = color_for_payload(axis_color(axis), highlighted, pick);
    const float radius = style.ring_radius * world_scale;
    const float visual_thickness = style.ring_visual_thickness * world_scale;

    append_volumetric_ring(
        draw,
        target.origin,
        normal,
        tangent,
        radius,
        visual_thickness,
        style.ring_segments,
        color,
        pick);
    draw.add_pick_ring(
        pick,
        target.origin,
        normal,
        radius,
        style.ring_pick_thickness * world_scale,
        style.pick_priority);
}

[[nodiscard]] float safe_world_scale(float scale_value)
{
    if (!std::isfinite(scale_value) || scale_value <= 0.0f) {
        return 1.0f;
    }

    return scale_value;
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

DebugDrawPickPayload transform_gizmo_pick_payload(
    RenderEntityId entity_id,
    DebugDrawPickRole role,
    DebugDrawAxis axis) noexcept
{
    return DebugDrawPickPayload {
        .owner_entity_id = entity_id.value,
        .role = role,
        .axis = axis,
    };
}

LightGizmoStyle default_light_gizmo_style() noexcept
{
    return {};
}

TransformGizmoStyle default_transform_gizmo_style() noexcept
{
    return {};
}

bool transform_gizmo_target_is_valid(const TransformGizmoTarget& target) noexcept
{
    return target.entity_id.is_valid();
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

void append_transform_gizmo(
    const TransformGizmoRequest& request,
    DebugDrawFrame& debug_draw,
    TransformGizmoStyle style)
{
    if (!request.target.has_value() || !transform_gizmo_target_is_valid(*request.target)) {
        return;
    }

    const TransformGizmoTarget& target = *request.target;
    const float world_scale = safe_world_scale(request.world_scale);
    DebugDrawList& draw = debug_draw.overlay;

    draw.reserve_pick_proxies(draw.pick_proxies().size() + 12);
    switch (request.tool) {
    case TransformGizmoTool::move:
        draw.reserve_triangles(
            draw.triangles().size() +
            static_cast<size_t>(std::max<uint32_t>(style.cone_segments, 8)) * 2U * 3U +
            3U * 12U +
            3U * 2U);
        append_move_axis(draw, target, DebugDrawAxis::x, style, world_scale, request.highlighted_payload);
        append_move_axis(draw, target, DebugDrawAxis::y, style, world_scale, request.highlighted_payload);
        append_move_axis(draw, target, DebugDrawAxis::z, style, world_scale, request.highlighted_payload);
        append_plane_handle(
            draw,
            target,
            DebugDrawAxis::x,
            DebugDrawPickRole::transform_move_plane,
            style,
            world_scale,
            request.highlighted_payload);
        append_plane_handle(
            draw,
            target,
            DebugDrawAxis::y,
            DebugDrawPickRole::transform_move_plane,
            style,
            world_scale,
            request.highlighted_payload);
        append_plane_handle(
            draw,
            target,
            DebugDrawAxis::z,
            DebugDrawPickRole::transform_move_plane,
            style,
            world_scale,
            request.highlighted_payload);
        break;
    case TransformGizmoTool::rotate:
        draw.reserve_triangles(
            draw.triangles().size() +
            static_cast<size_t>(std::max<uint32_t>(style.ring_segments, 16)) * 6U * 3U);
        append_rotate_axis(draw, target, DebugDrawAxis::x, style, world_scale, request.highlighted_payload);
        append_rotate_axis(draw, target, DebugDrawAxis::y, style, world_scale, request.highlighted_payload);
        append_rotate_axis(draw, target, DebugDrawAxis::z, style, world_scale, request.highlighted_payload);
        break;
    case TransformGizmoTool::scale:
        draw.reserve_triangles(draw.triangles().size() + 3U * 12U + 3U * 12U + 3U * 2U + 12U);
        append_scale_axis(draw, target, DebugDrawAxis::x, style, world_scale, request.highlighted_payload);
        append_scale_axis(draw, target, DebugDrawAxis::y, style, world_scale, request.highlighted_payload);
        append_scale_axis(draw, target, DebugDrawAxis::z, style, world_scale, request.highlighted_payload);
        append_plane_handle(
            draw,
            target,
            DebugDrawAxis::x,
            DebugDrawPickRole::transform_scale_plane,
            style,
            world_scale,
            request.highlighted_payload);
        append_plane_handle(
            draw,
            target,
            DebugDrawAxis::y,
            DebugDrawPickRole::transform_scale_plane,
            style,
            world_scale,
            request.highlighted_payload);
        append_plane_handle(
            draw,
            target,
            DebugDrawAxis::z,
            DebugDrawPickRole::transform_scale_plane,
            style,
            world_scale,
            request.highlighted_payload);
        append_uniform_scale_handle(draw, target, style, world_scale, request.highlighted_payload);
        break;
    }
}

} // namespace quader
