module;

#include <imgui.h>

#include <algorithm>
#include <limits>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

module quader.app.editor_viewport;

import quader.render.viewport_gizmos;
import quader.ui.viewport_host_panel;

namespace quader {
namespace {

constexpr uint32_t kMinimumRenderableViewportDimension = 64;

struct Ray {
    Vec3 origin;
    Vec3 direction;
};

struct RayHit {
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

ViewportId create_viewport_or_throw(IViewportRenderer& renderer, const char* viewport_name)
{
    std::optional<ViewportId> viewport_id = renderer.try_create_viewport();
    if (!viewport_id.has_value() || !viewport_id->is_valid()) {
        throw std::runtime_error(std::string("Renderer failed to create viewport: ") + viewport_name);
    }

    return *viewport_id;
}

ViewportNavigationSnapshot navigation_snapshot_from(
    const Input& input,
    const ResolvedEditorInput& resolved_input,
    bool viewport_hovered)
{
    const ViewportNavigationInputActions navigation_actions =
        viewport_navigation_input_actions_from(resolved_input);
    return ViewportNavigationSnapshot {
        .window_has_keyboard_focus = input.window_has_keyboard_focus(),
        .window_has_mouse_focus = input.window_has_mouse_focus(),
        .text_input_active = ImGui::GetIO().WantTextInput,
        .hovered = viewport_hovered,
        .navigation_activate_pressed = navigation_actions.activate_pressed,
        .navigation_activate_released = navigation_actions.activate_released,
        .navigation_activate_down = navigation_actions.activate_down,
        .fly_forward = navigation_actions.fly_forward_down,
        .fly_backward = navigation_actions.fly_backward_down,
        .fly_left = navigation_actions.fly_left_down,
        .fly_right = navigation_actions.fly_right_down,
        .boost = navigation_actions.boost_down,
        .mouse_delta = input.mouse_delta(),
        .mouse_wheel_delta = input.mouse_wheel_delta(),
    };
}

[[nodiscard]] std::optional<float> intersect_ray_sphere(
    const Ray& ray,
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
    const Ray& ray,
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
    const Ray& ray,
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
    const Ray& ray,
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
    const Ray local_ray {
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
    const Ray& ray,
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

[[nodiscard]] Ray viewport_ray_from_surface_pointer(
    const ViewportInstance& viewport,
    const ViewportSurfaceResult& surface,
    ViewportSurfacePointer pointer)
{
    const ViewportCamera camera = viewport.current_camera();
    const ViewportProjection projection = viewport.current_projection();
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
        return Ray {
            .origin = add(
                camera.position,
                add(scale(right, normalized_x * half_width), scale(up, normalized_y * half_height))),
            .direction = forward,
        };
    }

    const float half_fov_radians = degrees_to_radians(projection.vertical_fov_degrees) * 0.5f;
    const float half_height = std::tan(half_fov_radians);
    const float half_width = half_height * aspect;
    return Ray {
        .origin = camera.position,
        .direction = normalized_or(
            add(forward, add(scale(right, normalized_x * half_width), scale(up, normalized_y * half_height))),
            forward),
    };
}

[[nodiscard]] bool is_transform_gizmo_payload(DebugDrawPickPayload payload)
{
    return payload.role == DebugDrawPickRole::transform_move_axis ||
        payload.role == DebugDrawPickRole::transform_move_plane ||
        payload.role == DebugDrawPickRole::transform_rotate_axis ||
        payload.role == DebugDrawPickRole::transform_scale_axis ||
        payload.role == DebugDrawPickRole::transform_scale_plane ||
        payload.role == DebugDrawPickRole::transform_uniform_scale;
}

[[nodiscard]] std::optional<float> intersect_mesh_entity(const Ray& ray, const SceneEntity& entity)
{
    if (!entity.mesh.has_value()) {
        return std::nullopt;
    }

    const Vec3 safe_scale {
        std::abs(entity.transform.scale.x) > 0.000001f ? entity.transform.scale.x : 1.0f,
        std::abs(entity.transform.scale.y) > 0.000001f ? entity.transform.scale.y : 1.0f,
        std::abs(entity.transform.scale.z) > 0.000001f ? entity.transform.scale.z : 1.0f,
    };
    const Ray local_ray {
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

void upload_readback_frame_if_available(
    const ViewportRenderFrame& frame,
    ImGuiLayer& imgui)
{
    if (!frame.payload.temporary_readback.has_value() || !frame.payload.temporary_readback->has_pixels()) {
        return;
    }

    const TemporaryRgba8ReadbackFrame& readback = *frame.payload.temporary_readback;
    imgui.upload_viewport_texture(
        frame.viewport_id,
        readback.extent.width,
        readback.extent.height,
        readback.rgba.data());
}

[[nodiscard]] bool first_hit_is_better(const RayHit& candidate, const std::optional<RayHit>& current)
{
    if (!current.has_value()) {
        return true;
    }

    if (candidate.priority != current->priority) {
        return candidate.priority > current->priority;
    }

    if (candidate.distance < current->distance) {
        return true;
    }

    return false;
}

void consider_debug_pick_proxies(
    const Scene& scene,
    const Ray& ray,
    const DebugDrawList& draw,
    std::optional<RayHit>& best_hit)
{
    for (const DebugDrawPickProxy& proxy : draw.pick_proxies()) {
        if (!proxy.is_valid()) {
            continue;
        }

        const SceneEntityId owner { proxy.payload.owner_entity_id };
        if (!scene.contains(owner)) {
            continue;
        }

        std::optional<float> hit_distance;
        switch (proxy.kind) {
        case DebugDrawPickProxyKind::sphere:
            hit_distance = intersect_ray_sphere(ray, proxy.center, proxy.radius);
            break;
        case DebugDrawPickProxyKind::capsule:
            hit_distance = intersect_ray_capsule(ray, proxy.segment_a, proxy.segment_b, proxy.radius);
            break;
        case DebugDrawPickProxyKind::oriented_box:
            hit_distance = intersect_ray_oriented_box(
                ray,
                proxy.center,
                proxy.axis_x,
                proxy.axis_y,
                proxy.axis_z,
                proxy.half_extents);
            break;
        case DebugDrawPickProxyKind::ring:
            hit_distance = intersect_ray_ring(ray, proxy.center, proxy.normal, proxy.radius, proxy.thickness);
            break;
        }

        if (!hit_distance.has_value()) {
            continue;
        }

        RayHit candidate {
            .entity_id = owner,
            .debug_payload = proxy.payload,
            .distance = *hit_distance,
            .priority = proxy.priority,
        };
        if (first_hit_is_better(candidate, best_hit)) {
            best_hit = candidate;
        }
    }
}

} // namespace

EditorViewport::~EditorViewport()
{
    release_viewports();
}

EditorViewportFrameResult EditorViewport::render_frame(
    IViewportRenderer& renderer,
    ImGuiLayer& imgui,
    Scene& scene,
    const Input& input,
    EditorSelectionModel& selection,
    float delta_seconds)
{
    ensure_viewports(renderer);

    upload_completed_viewport_frames(renderer, imgui);

    EditorViewportFrameResult frame_result;
    std::vector<ViewportId> visible_viewport_ids;
    bool hover_payload_updated = false;
    const bool text_input_active = ImGui::GetIO().WantTextInput;
    const EditorInputEnvironment tool_input_environment =
        editor_input_environment_from_input(
            input,
            text_input_active,
            false,
            mouse_locked_viewport_.has_value(),
            transform_gizmo_.is_dragging());
    const ResolvedEditorInput tool_input = editor_input_.resolve(input, tool_input_environment);
    if (transform_gizmo_.update_tool_hotkeys(transform_tool_input_actions_from(tool_input))) {
        mark_scene_viewports_dirty();
    }
    render_scene_viewport_host(
        imgui,
        scene,
        input,
        selection,
        delta_seconds,
        visible_viewport_ids,
        hover_payload_updated,
        frame_result);
    if (!hover_payload_updated && transform_gizmo_.set_hovered_payload(std::nullopt)) {
        mark_scene_viewports_dirty();
    }
    release_navigation_capture_if_hidden(visible_viewport_ids);

    request_visible_viewport_frames(renderer, scene, selection, visible_viewport_ids);

    std::vector<ViewportId> owned_viewport_ids;
    append_owned_viewport_ids(owned_viewport_ids);
    imgui.prune_viewport_textures(owned_viewport_ids);
    return frame_result;
}

bool EditorViewport::wants_relative_mouse_mode() const noexcept
{
    return mouse_locked_viewport_.has_value();
}

void EditorViewport::mark_render_state_dirty()
{
    mark_all_viewports_dirty();
}

void EditorViewport::ensure_viewports(IViewportRenderer& renderer)
{
    if (renderer_ != nullptr) {
        return;
    }

    renderer_ = &renderer;

    scene_viewports_.reserve(4);
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Perspective"),
        "Perspective",
        ViewportCameraPreset::perspective));
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Top"),
        "Top",
        ViewportCameraPreset::top));
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Side"),
        "Side",
        ViewportCameraPreset::side));
    scene_viewports_.push_back(ViewportInstance::scene_viewport(
        create_viewport_or_throw(renderer, "Front"),
        "Front",
        ViewportCameraPreset::front));
    if (!scene_viewports_.empty()) {
        active_viewport_ = scene_viewports_.front().id();
    }
}

void EditorViewport::release_viewports()
{
    if (renderer_ == nullptr) {
        return;
    }

    for (const ViewportInstance& viewport : scene_viewports_) {
        renderer_->release_viewport(viewport.id());
    }

    scene_viewports_.clear();
    active_viewport_.reset();
    mouse_locked_viewport_.reset();
    renderer_ = nullptr;
}

void EditorViewport::append_owned_viewport_ids(std::vector<ViewportId>& viewport_ids) const
{
    viewport_ids.reserve(viewport_ids.size() + scene_viewports_.size());
    for (const ViewportInstance& viewport : scene_viewports_) {
        viewport_ids.push_back(viewport.id());
    }
}

void EditorViewport::render_scene_viewport_host(
    ImGuiLayer& imgui,
    Scene& scene,
    const Input& input,
    EditorSelectionModel& selection,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids,
    bool& hover_payload_updated,
    EditorViewportFrameResult& frame_result)
{
    std::vector<ViewportPaneInput> panes;
    panes.reserve(scene_viewports_.size());
    for (const ViewportInstance& viewport : scene_viewports_) {
        panes.push_back(ViewportPaneInput {
            .id = viewport.id(),
            .title = viewport.title(),
            .texture = imgui.viewport_texture(viewport.id()),
            .performance = performance_overlay_for(viewport, delta_seconds),
            .camera_preset = viewport.camera_preset(),
            .default_camera_preset = viewport.default_camera_preset(),
            .active = active_viewport_.has_value() && *active_viewport_ == viewport.id(),
            .camera_dropdown_enabled = true,
        });
    }

    EnvironmentLightingSettings environment_settings = scene.environment_settings();
    ViewportHostPanelResult result = draw_viewport_host_panel(layout_state_, environment_settings, panes);
    if (result.environment_changed) {
        scene.set_environment_settings(environment_settings);
        frame_result.scene_changed = true;
        mark_scene_viewports_dirty();
    }
    if (result.active_viewport.has_value()) {
        active_viewport_ = *result.active_viewport;
    }

    for (const ViewportPaneResult& pane : result.panes) {
        ViewportInstance* viewport = find_viewport(pane.id);
        if (viewport == nullptr) {
            continue;
        }

        if (pane.requested_camera_preset.has_value()) {
            viewport->set_camera_preset(*pane.requested_camera_preset);
        }

        update_viewport_from_surface(
            *viewport,
            scene,
            pane.surface,
            input,
            selection,
            delta_seconds,
            visible_viewport_ids,
            hover_payload_updated,
            frame_result);
    }
}

void EditorViewport::update_viewport_from_surface(
    ViewportInstance& viewport,
    Scene& scene,
    const ViewportSurfaceResult& surface,
    const Input& input,
    EditorSelectionModel& selection,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids,
    bool& hover_payload_updated,
    EditorViewportFrameResult& frame_result)
{
    if (!surface.visible) {
        return;
    }

    if (surface.requested_extent.width < kMinimumRenderableViewportDimension ||
        surface.requested_extent.height < kMinimumRenderableViewportDimension) {
        return;
    }

    visible_viewport_ids.push_back(viewport.id());
    viewport.set_requested_extent(surface.requested_extent);

    const bool navigation_captured =
        mouse_locked_viewport_.has_value() &&
        *mouse_locked_viewport_ == viewport.id();
    const EditorInputEnvironment viewport_input_environment =
        editor_input_environment_from_input(
            input,
            ImGui::GetIO().WantTextInput,
            surface.hovered,
            navigation_captured,
            transform_gizmo_.is_dragging());
    const ResolvedEditorInput viewport_input = editor_input_.resolve(input, viewport_input_environment);
    const ViewportNavigationInputActions navigation_actions =
        viewport_navigation_input_actions_from(viewport_input);

    if (surface.hovered || surface.clicked || navigation_actions.activate_down) {
        active_viewport_ = viewport.id();
    }

    if (surface.hovered && surface.pointer.has_value() && !transform_gizmo_.is_dragging()) {
        const ViewportPickResult hover_pick = pick_from_surface(scene, viewport, selection, surface);
        const std::optional<DebugDrawPickPayload> hover_payload =
            hover_pick.debug_payload.has_value() && is_transform_gizmo_payload(*hover_pick.debug_payload)
            ? hover_pick.debug_payload
            : std::nullopt;
        hover_payload_updated = true;
        if (transform_gizmo_.set_hovered_payload(hover_payload)) {
            mark_scene_viewports_dirty();
        }
    }

    if (transform_gizmo_.update_drag(scene, viewport.current_camera(), viewport.current_projection(), surface)) {
        active_viewport_ = viewport.id();
        frame_result.scene_changed = true;
        mark_scene_viewports_dirty();
    }

    if (surface.left_clicked && !surface.dragging && !transform_gizmo_.is_dragging()) {
        const ViewportPickResult pick = pick_from_surface(scene, viewport, selection, surface);
        if (pick.debug_payload.has_value() &&
            transform_gizmo_.try_begin_drag(
                *pick.debug_payload,
                scene,
                viewport.current_camera(),
                viewport.current_projection(),
                surface)) {
            selection.select_entity(SceneEntityId { pick.debug_payload->owner_entity_id });
            active_viewport_ = viewport.id();
        } else {
            selection.select_entity(pick.entity_id.value_or(SceneEntityId {}));
        }
        mark_scene_viewports_dirty();
    }

    const ViewportNavigationSnapshot navigation_snapshot =
        navigation_snapshot_from(input, viewport_input, surface.hovered);
    if (mouse_locked_viewport_ == viewport.id() &&
        !viewport.should_keep_navigation_mouse_lock(navigation_snapshot)) {
        mouse_locked_viewport_.reset();
    }

    if (!mouse_locked_viewport_.has_value() &&
        viewport.should_start_navigation_mouse_lock(navigation_snapshot)) {
        mouse_locked_viewport_ = viewport.id();
    }

    const bool navigation_mouse_locked =
        mouse_locked_viewport_.has_value() &&
        *mouse_locked_viewport_ == viewport.id();
    const bool navigation_changed =
        viewport.update_navigation(navigation_snapshot, navigation_mouse_locked, delta_seconds);
    (void)navigation_changed;
    if (navigation_mouse_locked) {
        active_viewport_ = viewport.id();
    }
}

void EditorViewport::release_navigation_capture_if_hidden(const std::vector<ViewportId>& visible_viewport_ids)
{
    if (!mouse_locked_viewport_.has_value()) {
        return;
    }

    const bool captured_viewport_is_visible =
        std::find(visible_viewport_ids.begin(), visible_viewport_ids.end(), *mouse_locked_viewport_) !=
        visible_viewport_ids.end();
    if (!captured_viewport_is_visible) {
        mouse_locked_viewport_.reset();
    }
}

void EditorViewport::request_visible_viewport_frames(
    IViewportRenderer& renderer,
    const Scene& scene,
    const EditorSelectionModel& selection,
    const std::vector<ViewportId>& visible_viewport_ids)
{
    for (ViewportId viewport_id : visible_viewport_ids) {
        ViewportInstance* viewport = find_viewport(viewport_id);
        if (viewport == nullptr) {
            continue;
        }

        const std::optional<TransformGizmoRequest> transform_gizmo =
            transform_gizmo_.render_request_for_selection(
                scene,
                selection.selected_entity(),
                viewport->current_camera(),
                viewport->current_projection());
        std::optional<ViewportRenderRequest> request =
            viewport->try_begin_render_request(scene, transform_gizmo);
        if (!request.has_value()) {
            continue;
        }

        if (!renderer.request_viewport_frame(*request)) {
            viewport->fail_pending_request();
        }
    }
}

void EditorViewport::upload_completed_viewport_frames(IViewportRenderer& renderer, ImGuiLayer& imgui)
{
    auto consume_viewport = [&renderer, &imgui](ViewportInstance& viewport) {
        std::optional<ViewportRenderFrame> frame = renderer.try_consume_viewport_frame(viewport.id());
        if (!frame.has_value()) {
            return;
        }

        const ViewportFrameAcceptance acceptance = viewport.accept_frame(*frame);
        if (!acceptance.accepted) {
            return;
        }

        upload_readback_frame_if_available(*frame, imgui);
    };

    for (ViewportInstance& viewport : scene_viewports_) {
        consume_viewport(viewport);
    }
}

ViewportInstance* EditorViewport::find_viewport(ViewportId viewport_id)
{
    for (ViewportInstance& viewport : scene_viewports_) {
        if (viewport.id() == viewport_id) {
            return &viewport;
        }
    }

    return nullptr;
}

const ViewportInstance* EditorViewport::find_viewport(ViewportId viewport_id) const
{
    for (const ViewportInstance& viewport : scene_viewports_) {
        if (viewport.id() == viewport_id) {
            return &viewport;
        }
    }

    return nullptr;
}

ViewportPerformanceOverlay EditorViewport::performance_overlay_for(
    const ViewportInstance& viewport,
    float delta_seconds) const
{
    return ViewportPerformanceOverlay {
        .enabled = true,
        .frame_delta_seconds = delta_seconds,
        .render_request_pending = viewport.request_pending(),
        .dirty = viewport.is_dirty(),
    };
}

EditorViewport::ViewportPickResult EditorViewport::pick_from_surface(
    const Scene& scene,
    const ViewportInstance& viewport,
    const EditorSelectionModel& selection,
    const ViewportSurfaceResult& surface) const
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

    const Ray ray = viewport_ray_from_surface_pointer(viewport, surface, *pointer);
    std::optional<RayHit> best_hit;

    SceneRenderSnapshot snapshot = build_scene_render_snapshot(scene);
    DebugDrawFrame debug_draw;
    append_light_gizmos(snapshot, debug_draw);
    const std::optional<TransformGizmoRequest> transform_gizmo =
        transform_gizmo_.render_request_for_selection(
            scene,
            selection.selected_entity(),
            viewport.current_camera(),
            viewport.current_projection());
    if (transform_gizmo.has_value()) {
        append_transform_gizmo(*transform_gizmo, debug_draw);
    }
    consider_debug_pick_proxies(scene, ray, debug_draw.depth_tested, best_hit);
    consider_debug_pick_proxies(scene, ray, debug_draw.overlay, best_hit);

    for (const SceneEntity& entity : scene.entities()) {
        std::optional<float> hit_distance = intersect_mesh_entity(ray, entity);

        if (!hit_distance.has_value()) {
            continue;
        }

        RayHit candidate {
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

    return ViewportPickResult {
        .entity_id = best_hit->entity_id,
        .debug_payload = best_hit->debug_payload,
    };
}

void EditorViewport::mark_scene_viewports_dirty()
{
    for (ViewportInstance& viewport : scene_viewports_) {
        viewport.mark_dirty();
    }
}

void EditorViewport::mark_all_viewports_dirty()
{
    mark_scene_viewports_dirty();
}

} // namespace quader
