module;

#include <algorithm>
#include <cmath>
#include <optional>

module quader.app.transform_gizmo;

namespace quader {
namespace {

constexpr float pi = 3.14159265358979323846f;
constexpr float min_axis_projection_pixels = 4.0f;
constexpr float min_scale_reference_pixels = 48.0f;
constexpr float scale_reference_viewport_fraction = 0.18f;
constexpr float min_transform_scale = 0.01f;
constexpr float inverse_sqrt_two = 0.70710678118f;
constexpr float edge_on_rotation_cosine_threshold = 0.08715574274f; // cos(85 degrees).

[[nodiscard]] float transform_gizmo_world_scale(Vec3 origin, ViewportCamera camera, ViewportProjection projection);

[[nodiscard]] float degrees_to_radians(float degrees)
{
    return degrees * pi / 180.0f;
}

[[nodiscard]] float radians_to_degrees(float radians)
{
    return radians * 180.0f / pi;
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

[[nodiscard]] float distance_between(Vec3 left, Vec3 right)
{
    return std::sqrt(squared_length(subtract(left, right)));
}

[[nodiscard]] float visual_gizmo_axis_world_length(Vec3 origin, ViewportCamera camera, ViewportProjection projection)
{
    const TransformGizmoStyle style = default_transform_gizmo_style();
    return style.axis_length * transform_gizmo_world_scale(origin, camera, projection);
}

[[nodiscard]] float visual_gizmo_ring_world_radius(Vec3 origin, ViewportCamera camera, ViewportProjection projection)
{
    const TransformGizmoStyle style = default_transform_gizmo_style();
    return style.ring_radius * transform_gizmo_world_scale(origin, camera, projection);
}

[[nodiscard]] float scale_reference_pixels(ViewportImageBounds bounds)
{
    const float smaller_dimension = std::min(bounds.width, bounds.height);
    return std::max(min_scale_reference_pixels, smaller_dimension * scale_reference_viewport_fraction);
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

[[nodiscard]] Vec3 transform_axis_direction(
    const TransformComponent& transform,
    DebugDrawAxis axis,
    TransformGizmoSpace space)
{
    const Vec3 base_axis = axis_vector(axis);
    if (space == TransformGizmoSpace::local) {
        return normalized_or(rotate_by_quaternion(base_axis, transform.rotation), base_axis);
    }

    return base_axis;
}

void plane_component_axes(DebugDrawAxis normal_axis, DebugDrawAxis& axis_u, DebugDrawAxis& axis_v)
{
    switch (normal_axis) {
    case DebugDrawAxis::x:
        axis_u = DebugDrawAxis::y;
        axis_v = DebugDrawAxis::z;
        break;
    case DebugDrawAxis::y:
        axis_u = DebugDrawAxis::x;
        axis_v = DebugDrawAxis::z;
        break;
    case DebugDrawAxis::z:
        axis_u = DebugDrawAxis::x;
        axis_v = DebugDrawAxis::y;
        break;
    case DebugDrawAxis::none:
        axis_u = DebugDrawAxis::x;
        axis_v = DebugDrawAxis::y;
        break;
    }
}

void plane_drag_axes(
    const TransformComponent& transform,
    DebugDrawAxis normal_axis,
    TransformGizmoSpace space,
    Vec3& axis_u,
    Vec3& axis_v)
{
    DebugDrawAxis component_u = DebugDrawAxis::x;
    DebugDrawAxis component_v = DebugDrawAxis::y;
    plane_component_axes(normal_axis, component_u, component_v);
    axis_u = transform_axis_direction(transform, component_u, space);
    axis_v = transform_axis_direction(transform, component_v, space);
}

void add_axis_component(Vec3& value, DebugDrawAxis axis, float amount)
{
    switch (axis) {
    case DebugDrawAxis::x:
        value.x += amount;
        break;
    case DebugDrawAxis::y:
        value.y += amount;
        break;
    case DebugDrawAxis::z:
        value.z += amount;
        break;
    case DebugDrawAxis::none:
        break;
    }
}

void set_axis_component_min(Vec3& value, DebugDrawAxis axis, float amount)
{
    amount = std::max(min_transform_scale, amount);
    switch (axis) {
    case DebugDrawAxis::x:
        value.x = amount;
        break;
    case DebugDrawAxis::y:
        value.y = amount;
        break;
    case DebugDrawAxis::z:
        value.z = amount;
        break;
    case DebugDrawAxis::none:
        break;
    }
}

[[nodiscard]] float axis_component(Vec3 value, DebugDrawAxis axis)
{
    switch (axis) {
    case DebugDrawAxis::x:
        return value.x;
    case DebugDrawAxis::y:
        return value.y;
    case DebugDrawAxis::z:
        return value.z;
    case DebugDrawAxis::none:
        break;
    }

    return 0.0f;
}

[[nodiscard]] std::optional<ViewportSurfacePointer> project_world_to_surface(
    Vec3 world_position,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return std::nullopt;
    }

    const float aspect = bounds.width / bounds.height;
    const Vec3 forward = normalized_or(camera.forward, { 0.0f, 0.0f, -1.0f });
    const Vec3 right = normalized_or(cross(forward, camera.up), { 1.0f, 0.0f, 0.0f });
    const Vec3 up = normalized_or(cross(right, forward), { 0.0f, 1.0f, 0.0f });
    const Vec3 camera_to_point = subtract(world_position, camera.position);

    float normalized_x = 0.0f;
    float normalized_y = 0.0f;
    if (projection.kind == ViewportProjectionKind::orthographic) {
        const float half_height = std::max(0.001f, projection.orthographic_size * 0.5f);
        const float half_width = std::max(0.001f, half_height * aspect);
        normalized_x = dot(camera_to_point, right) / half_width;
        normalized_y = dot(camera_to_point, up) / half_height;
    } else {
        const float depth = dot(camera_to_point, forward);
        if (depth <= 0.001f) {
            return std::nullopt;
        }

        const float half_fov = degrees_to_radians(projection.vertical_fov_degrees) * 0.5f;
        const float half_height = std::tan(half_fov) * depth;
        const float half_width = half_height * aspect;
        if (half_height <= 0.000001f || half_width <= 0.000001f) {
            return std::nullopt;
        }

        normalized_x = dot(camera_to_point, right) / half_width;
        normalized_y = dot(camera_to_point, up) / half_height;
    }

    return ViewportSurfacePointer {
        .local_x = (normalized_x + 1.0f) * 0.5f * bounds.width,
        .local_y = (1.0f - normalized_y) * 0.5f * bounds.height,
    };
}

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

[[nodiscard]] std::optional<Ray> viewport_ray_from_surface_pointer(
    ViewportSurfacePointer pointer,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) {
        return std::nullopt;
    }

    const float aspect = bounds.width / bounds.height;
    const float normalized_x = (pointer.local_x / bounds.width) * 2.0f - 1.0f;
    const float normalized_y = 1.0f - (pointer.local_y / bounds.height) * 2.0f;
    const Vec3 forward = normalized_or(camera.forward, { 0.0f, 0.0f, -1.0f });
    const Vec3 right = normalized_or(cross(forward, camera.up), { 1.0f, 0.0f, 0.0f });
    const Vec3 up = normalized_or(cross(right, forward), { 0.0f, 1.0f, 0.0f });

    if (projection.kind == ViewportProjectionKind::orthographic) {
        const float half_height = projection.orthographic_size * 0.5f;
        const float half_width = half_height * aspect;
        return Ray {
            .origin = add(
                camera.position,
                add(scale(right, normalized_x * half_width), scale(up, normalized_y * half_height))),
            .direction = forward,
        };
    }

    const float half_fov = degrees_to_radians(projection.vertical_fov_degrees) * 0.5f;
    const float half_height = std::tan(half_fov);
    const float half_width = half_height * aspect;
    return Ray {
        .origin = camera.position,
        .direction = normalized_or(
            add(forward, add(scale(right, normalized_x * half_width), scale(up, normalized_y * half_height))),
            forward),
    };
}

[[nodiscard]] std::optional<Vec3> intersect_ray_plane(
    Ray ray,
    Vec3 plane_origin,
    Vec3 plane_normal)
{
    plane_normal = normalized_or(plane_normal, { 0.0f, 1.0f, 0.0f });
    const float denominator = dot(ray.direction, plane_normal);
    if (std::abs(denominator) <= 0.000001f) {
        return std::nullopt;
    }

    const float t = dot(subtract(plane_origin, ray.origin), plane_normal) / denominator;
    if (t < 0.0f) {
        return std::nullopt;
    }

    return add(ray.origin, scale(ray.direction, t));
}

[[nodiscard]] std::optional<Vec3> rotation_vector_from_pointer(
    ViewportSurfacePointer pointer,
    Vec3 origin,
    Vec3 axis,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    const std::optional<Ray> ray = viewport_ray_from_surface_pointer(pointer, camera, projection, bounds);
    if (!ray.has_value()) {
        return std::nullopt;
    }

    const std::optional<Vec3> hit = intersect_ray_plane(*ray, origin, axis);
    if (!hit.has_value()) {
        return std::nullopt;
    }

    return normalized_or(subtract(*hit, origin), {});
}

[[nodiscard]] std::optional<Vec3> plane_hit_from_pointer(
    ViewportSurfacePointer pointer,
    Vec3 origin,
    Vec3 normal,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    const std::optional<Ray> ray = viewport_ray_from_surface_pointer(pointer, camera, projection, bounds);
    if (!ray.has_value()) {
        return std::nullopt;
    }

    return intersect_ray_plane(*ray, origin, normal);
}

[[nodiscard]] Vec3 view_plane_normal(Vec3 origin, ViewportCamera camera, ViewportProjection projection)
{
    if (projection.kind == ViewportProjectionKind::perspective) {
        return normalized_or(subtract(origin, camera.position), normalized_or(camera.forward, { 0.0f, 0.0f, -1.0f }));
    }

    return normalized_or(camera.forward, { 0.0f, 0.0f, -1.0f });
}

[[nodiscard]] Vec3 axis_drag_plane_normal(Vec3 axis, Vec3 view_normal)
{
    axis = normalized_or(axis, { 1.0f, 0.0f, 0.0f });
    view_normal = normalized_or(view_normal, { 0.0f, 0.0f, -1.0f });
    Vec3 normal = cross(axis, cross(axis, view_normal));
    if (squared_length(normal) <= 0.000001f) {
        normal = cross(axis, { 0.0f, 1.0f, 0.0f });
        if (squared_length(normal) <= 0.000001f) {
            normal = cross(axis, { 1.0f, 0.0f, 0.0f });
        }
    }

    return normalized_or(normal, view_normal);
}

[[nodiscard]] std::optional<Vec3> axis_drag_plane_hit_from_pointer(
    ViewportSurfacePointer pointer,
    Vec3 origin,
    Vec3 axis,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    return plane_hit_from_pointer(
        pointer,
        origin,
        axis_drag_plane_normal(axis, view_plane_normal(origin, camera, projection)),
        camera,
        projection,
        bounds);
}

[[nodiscard]] float signed_angle_around_axis(Vec3 from, Vec3 to, Vec3 axis)
{
    from = normalized_or(from, {});
    to = normalized_or(to, {});
    axis = normalized_or(axis, { 0.0f, 1.0f, 0.0f });
    if (squared_length(from) <= 0.000001f || squared_length(to) <= 0.000001f) {
        return 0.0f;
    }

    return std::atan2(dot(axis, cross(from, to)), dot(from, to));
}

[[nodiscard]] std::optional<float> world_axis_drag_amount(
    Vec3 origin,
    Vec3 direction,
    ViewportSurfacePointer start_pointer,
    ViewportSurfacePointer current_pointer,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    direction = normalized_or(direction, {});
    if (squared_length(direction) <= 0.000001f) {
        return std::nullopt;
    }

    const std::optional<Vec3> start_hit =
        axis_drag_plane_hit_from_pointer(start_pointer, origin, direction, camera, projection, bounds);
    const std::optional<Vec3> current_hit =
        axis_drag_plane_hit_from_pointer(current_pointer, origin, direction, camera, projection, bounds);
    if (!start_hit.has_value() || !current_hit.has_value()) {
        return std::nullopt;
    }

    return dot(subtract(*current_hit, *start_hit), direction);
}

[[nodiscard]] std::optional<float> screen_axis_scale_drag_amount(
    Vec3 origin,
    Vec3 direction,
    ViewportSurfacePointer start_pointer,
    ViewportSurfacePointer current_pointer,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    direction = normalized_or(direction, {});
    if (squared_length(direction) <= 0.000001f) {
        return std::nullopt;
    }

    const float visual_axis_length = visual_gizmo_axis_world_length(origin, camera, projection);
    const std::optional<ViewportSurfacePointer> projected_origin =
        project_world_to_surface(origin, camera, projection, bounds);
    const std::optional<ViewportSurfacePointer> projected_axis =
        project_world_to_surface(add(origin, scale(direction, visual_axis_length)), camera, projection, bounds);
    if (!projected_origin.has_value() || !projected_axis.has_value()) {
        return std::nullopt;
    }

    const float axis_x = projected_axis->local_x - projected_origin->local_x;
    const float axis_y = projected_axis->local_y - projected_origin->local_y;
    const float axis_length = std::sqrt(axis_x * axis_x + axis_y * axis_y);
    if (axis_length < min_axis_projection_pixels) {
        return std::nullopt;
    }

    const float screen_axis_x = axis_x / axis_length;
    const float screen_axis_y = axis_y / axis_length;
    const float drag_x = current_pointer.local_x - start_pointer.local_x;
    const float drag_y = current_pointer.local_y - start_pointer.local_y;
    return (drag_x * screen_axis_x + drag_y * screen_axis_y) / scale_reference_pixels(bounds);
}

[[nodiscard]] float uniform_scale_amount(
    ViewportSurfacePointer start_pointer,
    ViewportSurfacePointer current_pointer,
    ViewportImageBounds bounds)
{
    const float drag = (
        (current_pointer.local_x - start_pointer.local_x) +
        (start_pointer.local_y - current_pointer.local_y)) * inverse_sqrt_two;
    return drag / scale_reference_pixels(bounds);
}

[[nodiscard]] std::optional<float> rotation_angle_from_pointer_drag(
    Vec3 origin,
    Vec3 axis,
    ViewportSurfacePointer start_pointer,
    ViewportSurfacePointer current_pointer,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    axis = normalized_or(axis, {});
    if (squared_length(axis) <= 0.000001f) {
        return std::nullopt;
    }

    const Vec3 view_normal = view_plane_normal(origin, camera, projection);
    const bool axis_is_edge_on = std::abs(dot(view_normal, axis)) < edge_on_rotation_cosine_threshold;
    if (axis_is_edge_on) {
        const std::optional<Vec3> start_hit =
            plane_hit_from_pointer(start_pointer, origin, view_normal, camera, projection, bounds);
        const std::optional<Vec3> current_hit =
            plane_hit_from_pointer(current_pointer, origin, view_normal, camera, projection, bounds);
        if (!start_hit.has_value() || !current_hit.has_value()) {
            return std::nullopt;
        }

        const Vec3 projection_axis = normalized_or(cross(view_normal, axis), {});
        if (squared_length(projection_axis) <= 0.000001f) {
            return std::nullopt;
        }

        const float projected_motion = dot(subtract(*current_hit, *start_hit), projection_axis);
        const float radius = std::max(0.000001f, visual_gizmo_ring_world_radius(origin, camera, projection));
        return (projected_motion * (pi * 0.5f)) / radius;
    }

    const std::optional<Vec3> start_vector =
        rotation_vector_from_pointer(start_pointer, origin, axis, camera, projection, bounds);
    const std::optional<Vec3> current_vector =
        rotation_vector_from_pointer(current_pointer, origin, axis, camera, projection, bounds);
    if (!start_vector.has_value() || !current_vector.has_value()) {
        return std::nullopt;
    }

    return signed_angle_around_axis(*start_vector, *current_vector, axis);
}

[[nodiscard]] bool rotation_drag_can_start(
    Vec3 origin,
    Vec3 axis,
    ViewportSurfacePointer pointer,
    ViewportCamera camera,
    ViewportProjection projection,
    ViewportImageBounds bounds)
{
    axis = normalized_or(axis, {});
    if (squared_length(axis) <= 0.000001f) {
        return false;
    }

    const Vec3 view_normal = view_plane_normal(origin, camera, projection);
    if (std::abs(dot(view_normal, axis)) < edge_on_rotation_cosine_threshold) {
        return plane_hit_from_pointer(pointer, origin, view_normal, camera, projection, bounds).has_value();
    }

    return rotation_vector_from_pointer(pointer, origin, axis, camera, projection, bounds).has_value();
}

[[nodiscard]] float transform_gizmo_world_scale(Vec3 origin, ViewportCamera camera, ViewportProjection projection)
{
    if (projection.kind == ViewportProjectionKind::orthographic) {
        return std::clamp(projection.orthographic_size * 0.18f, 0.35f, 8.0f);
    }

    return std::clamp(distance_between(origin, camera.position) * 0.12f, 0.35f, 8.0f);
}

[[nodiscard]] bool is_transform_pick_role(DebugDrawPickRole role)
{
    return role == DebugDrawPickRole::transform_move_axis ||
        role == DebugDrawPickRole::transform_move_plane ||
        role == DebugDrawPickRole::transform_rotate_axis ||
        role == DebugDrawPickRole::transform_scale_axis ||
        role == DebugDrawPickRole::transform_scale_plane ||
        role == DebugDrawPickRole::transform_uniform_scale;
}

} // namespace

bool TransformGizmoController::update_tool_hotkeys(const TransformToolInputActions& actions)
{
    const TransformGizmoTool previous_tool = active_tool_;
    const TransformGizmoSpace previous_space = active_space_;
    if (actions.select_move_pressed) {
        active_tool_ = TransformGizmoTool::move;
    } else if (actions.select_rotate_pressed) {
        active_tool_ = TransformGizmoTool::rotate;
    } else if (actions.select_scale_pressed) {
        active_tool_ = TransformGizmoTool::scale;
    }

    if (actions.toggle_space_pressed) {
        active_space_ = active_space_ == TransformGizmoSpace::global
            ? TransformGizmoSpace::local
            : TransformGizmoSpace::global;
    }

    return active_tool_ != previous_tool || active_space_ != previous_space;
}

TransformGizmoTool TransformGizmoController::active_tool() const noexcept
{
    return active_tool_;
}

TransformGizmoSpace TransformGizmoController::active_space() const noexcept
{
    return active_space_;
}

bool TransformGizmoController::is_dragging() const noexcept
{
    return active_drag_.has_value();
}

bool TransformGizmoController::set_hovered_payload(std::optional<DebugDrawPickPayload> payload)
{
    if (payload.has_value() && !is_transform_pick_role(payload->role)) {
        payload.reset();
    }

    if (hovered_payload_ == payload) {
        return false;
    }

    hovered_payload_ = payload;
    return true;
}

std::optional<TransformGizmoRequest> TransformGizmoController::render_request_for_selection(
    const Scene& scene,
    std::optional<SceneEntityId> selected_entity,
    ViewportCamera camera,
    ViewportProjection projection) const
{
    if (!selected_entity.has_value()) {
        return std::nullopt;
    }

    const TransformComponent* transform = scene.transform(*selected_entity);
    if (transform == nullptr) {
        return std::nullopt;
    }

    std::optional<DebugDrawPickPayload> highlighted_payload;
    if (active_drag_.has_value() && active_drag_->entity_id == *selected_entity) {
        highlighted_payload = transform_gizmo_pick_payload(
            RenderEntityId { selected_entity->value },
            active_drag_->role,
            active_drag_->axis);
    } else if (
        hovered_payload_.has_value() &&
        hovered_payload_->owner_entity_id == selected_entity->value &&
        is_transform_pick_role(hovered_payload_->role)) {
        highlighted_payload = hovered_payload_;
    }

    return TransformGizmoRequest {
        .tool = active_tool_,
        .space = active_space_,
        .target = TransformGizmoTarget {
            .entity_id = RenderEntityId { selected_entity->value },
            .origin = transform->position,
            .x_axis = transform_axis_direction(*transform, DebugDrawAxis::x, active_space_),
            .y_axis = transform_axis_direction(*transform, DebugDrawAxis::y, active_space_),
            .z_axis = transform_axis_direction(*transform, DebugDrawAxis::z, active_space_),
        },
        .highlighted_payload = highlighted_payload,
        .world_scale = transform_gizmo_world_scale(transform->position, camera, projection),
    };
}

bool TransformGizmoController::try_begin_drag(
    DebugDrawPickPayload payload,
    const Scene& scene,
    ViewportCamera camera,
    ViewportProjection projection,
    const ViewportSurfaceResult& surface)
{
    if (!payload.is_valid() || !is_transform_pick_role(payload.role) || !surface.pointer.has_value()) {
        return false;
    }

    const SceneEntityId entity_id { payload.owner_entity_id };
    const TransformComponent* transform = scene.transform(entity_id);
    if (transform == nullptr) {
        return false;
    }

    Vec3 plane_axis_u;
    Vec3 plane_axis_v;
    plane_drag_axes(*transform, payload.axis, active_space_, plane_axis_u, plane_axis_v);
    const Vec3 axis_direction = transform_axis_direction(*transform, payload.axis, active_space_);
    std::optional<Vec3> start_plane_hit;
    if (
        payload.role == DebugDrawPickRole::transform_move_plane ||
        payload.role == DebugDrawPickRole::transform_scale_plane) {
        start_plane_hit = plane_hit_from_pointer(
            *surface.pointer,
            transform->position,
            axis_direction,
            camera,
            projection,
            surface.image_bounds);
        if (!start_plane_hit.has_value()) {
            return false;
        }
    }

    if (payload.role == DebugDrawPickRole::transform_rotate_axis) {
        if (!rotation_drag_can_start(
                transform->position,
                axis_direction,
                *surface.pointer,
                camera,
                projection,
                surface.image_bounds)) {
            return false;
        }
    }

    active_drag_ = ActiveDrag {
        .entity_id = entity_id,
        .role = payload.role,
        .axis = payload.axis,
        .axis_direction = axis_direction,
        .plane_axis_u = plane_axis_u,
        .plane_axis_v = plane_axis_v,
        .start_plane_hit = start_plane_hit,
        .start_transform = *transform,
        .origin = transform->position,
        .start_pointer = *surface.pointer,
    };
    return true;
}

bool TransformGizmoController::update_drag(
    Scene& scene,
    ViewportCamera camera,
    ViewportProjection projection,
    const ViewportSurfaceResult& surface)
{
    if (!active_drag_.has_value()) {
        return false;
    }

    if (surface.left_released || !surface.left_down) {
        active_drag_.reset();
        return false;
    }

    if (!surface.pointer.has_value()) {
        return false;
    }

    TransformComponent* transform = scene.transform(active_drag_->entity_id);
    if (transform == nullptr) {
        active_drag_.reset();
        return false;
    }

    bool changed = false;
    if (active_drag_->role == DebugDrawPickRole::transform_move_axis) {
        const std::optional<float> amount = world_axis_drag_amount(
            active_drag_->origin,
            active_drag_->axis_direction,
            active_drag_->start_pointer,
            *surface.pointer,
            camera,
            projection,
            surface.image_bounds);
        if (amount.has_value()) {
            transform->position =
                add(active_drag_->start_transform.position, scale(active_drag_->axis_direction, *amount));
            changed = true;
        }
    } else if (active_drag_->role == DebugDrawPickRole::transform_move_plane) {
        const std::optional<Vec3> current_plane_hit = plane_hit_from_pointer(
            *surface.pointer,
            active_drag_->origin,
            active_drag_->axis_direction,
            camera,
            projection,
            surface.image_bounds);
        if (active_drag_->start_plane_hit.has_value() && current_plane_hit.has_value()) {
            const Vec3 plane_motion = subtract(*current_plane_hit, *active_drag_->start_plane_hit);
            const float amount_u = dot(plane_motion, active_drag_->plane_axis_u);
            const float amount_v = dot(plane_motion, active_drag_->plane_axis_v);
            transform->position = add(
                active_drag_->start_transform.position,
                add(scale(active_drag_->plane_axis_u, amount_u), scale(active_drag_->plane_axis_v, amount_v)));
            changed = true;
        }
    } else if (active_drag_->role == DebugDrawPickRole::transform_rotate_axis) {
        const std::optional<float> angle_radians = rotation_angle_from_pointer_drag(
            active_drag_->origin,
            active_drag_->axis_direction,
            active_drag_->start_pointer,
            *surface.pointer,
            camera,
            projection,
            surface.image_bounds);
        if (angle_radians.has_value()) {
            Vec3 rotation = active_drag_->start_transform.get_rotation_degrees();
            add_axis_component(rotation, active_drag_->axis, radians_to_degrees(*angle_radians));
            transform->set_rotation_degrees(rotation);
            changed = true;
        }
    } else if (active_drag_->role == DebugDrawPickRole::transform_scale_axis) {
        if (!scene_entity_can_scale(scene, active_drag_->entity_id)) {
            return false;
        }

        const std::optional<float> amount = screen_axis_scale_drag_amount(
            active_drag_->origin,
            active_drag_->axis_direction,
            active_drag_->start_pointer,
            *surface.pointer,
            camera,
            projection,
            surface.image_bounds);
        if (amount.has_value()) {
            Vec3 new_scale = active_drag_->start_transform.scale;
            set_axis_component_min(
                new_scale,
                active_drag_->axis,
                axis_component(active_drag_->start_transform.scale, active_drag_->axis) + *amount);
            transform->scale = new_scale;
            changed = true;
        }
    } else if (active_drag_->role == DebugDrawPickRole::transform_scale_plane) {
        if (!scene_entity_can_scale(scene, active_drag_->entity_id)) {
            return false;
        }

        const std::optional<float> amount_u = screen_axis_scale_drag_amount(
            active_drag_->origin,
            active_drag_->plane_axis_u,
            active_drag_->start_pointer,
            *surface.pointer,
            camera,
            projection,
            surface.image_bounds);
        const std::optional<float> amount_v = screen_axis_scale_drag_amount(
            active_drag_->origin,
            active_drag_->plane_axis_v,
            active_drag_->start_pointer,
            *surface.pointer,
            camera,
            projection,
            surface.image_bounds);
        if (amount_u.has_value() && amount_v.has_value()) {
            DebugDrawAxis component_u = DebugDrawAxis::x;
            DebugDrawAxis component_v = DebugDrawAxis::y;
            plane_component_axes(active_drag_->axis, component_u, component_v);
            Vec3 new_scale = active_drag_->start_transform.scale;
            set_axis_component_min(
                new_scale,
                component_u,
                axis_component(active_drag_->start_transform.scale, component_u) + *amount_u);
            set_axis_component_min(
                new_scale,
                component_v,
                axis_component(active_drag_->start_transform.scale, component_v) + *amount_v);
            transform->scale = new_scale;
            changed = true;
        }
    } else if (active_drag_->role == DebugDrawPickRole::transform_uniform_scale) {
        if (!scene_entity_can_scale(scene, active_drag_->entity_id)) {
            return false;
        }

        const float amount = uniform_scale_amount(
            active_drag_->start_pointer,
            *surface.pointer,
            surface.image_bounds);
        transform->scale = {
            std::max(min_transform_scale, active_drag_->start_transform.scale.x + amount),
            std::max(min_transform_scale, active_drag_->start_transform.scale.y + amount),
            std::max(min_transform_scale, active_drag_->start_transform.scale.z + amount),
        };
        changed = true;
    }

    return changed;
}

void TransformGizmoController::cancel_drag()
{
    active_drag_.reset();
}

bool scene_entity_can_scale(const Scene& scene, SceneEntityId entity_id)
{
    return scene.mesh(entity_id) != nullptr;
}

} // namespace quader
