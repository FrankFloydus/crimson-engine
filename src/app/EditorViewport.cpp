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

bool is_viewport_movement_key_down(const Input& input)
{
    return input.is_key_down(AppKey::w) ||
        input.is_key_down(AppKey::a) ||
        input.is_key_down(AppKey::s) ||
        input.is_key_down(AppKey::d) ||
        input.is_key_down(AppKey::q) ||
        input.is_key_down(AppKey::e);
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
    bool viewport_hovered)
{
    return ViewportNavigationSnapshot {
        .window_has_keyboard_focus = input.window_has_keyboard_focus(),
        .window_has_mouse_focus = input.window_has_mouse_focus(),
        .text_input_active = ImGui::GetIO().WantTextInput,
        .hovered = viewport_hovered,
        .right_mouse_pressed = input.was_mouse_button_pressed(AppMouseButton::right),
        .right_mouse_released = input.was_mouse_button_released(AppMouseButton::right),
        .right_mouse_down = input.is_mouse_button_down(AppMouseButton::right),
        .move_forward = input.is_key_down(AppKey::w),
        .move_backward = input.is_key_down(AppKey::s),
        .move_left = input.is_key_down(AppKey::a),
        .move_right = input.is_key_down(AppKey::d),
        .move_up = input.is_key_down(AppKey::e),
        .move_down = input.is_key_down(AppKey::q),
        .boost = input.is_key_down(AppKey::left_shift) || input.is_key_down(AppKey::right_shift),
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

[[nodiscard]] Ray viewport_ray_from_surface(
    const ViewportInstance& viewport,
    const ViewportSurfaceResult& surface)
{
    const ViewportCamera camera = viewport.current_camera();
    const ViewportProjection projection = viewport.current_projection();
    const float aspect =
        surface.image_bounds.height > 0.0f ? surface.image_bounds.width / surface.image_bounds.height : 1.0f;
    const float normalized_x = surface.click.has_value() && surface.image_bounds.width > 0.0f
        ? (surface.click->local_x / surface.image_bounds.width) * 2.0f - 1.0f
        : 0.0f;
    const float normalized_y = surface.click.has_value() && surface.image_bounds.height > 0.0f
        ? 1.0f - (surface.click->local_y / surface.image_bounds.height) * 2.0f
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

    if (candidate.distance < current->distance) {
        return true;
    }

    return candidate.distance == current->distance && candidate.priority > current->priority;
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
        }

        if (!hit_distance.has_value()) {
            continue;
        }

        RayHit candidate {
            .entity_id = owner,
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
    const Scene& scene,
    const Input& input,
    float delta_seconds)
{
    ensure_viewports(renderer);

    upload_completed_viewport_frames(renderer, imgui);

    EditorViewportFrameResult frame_result;
    std::vector<ViewportId> visible_viewport_ids;
    render_scene_viewport_host(imgui, scene, input, delta_seconds, visible_viewport_ids, frame_result);
    release_navigation_capture_if_hidden(visible_viewport_ids);

    request_visible_viewport_frames(renderer, scene, visible_viewport_ids);

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
    const Scene& scene,
    const Input& input,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids,
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

    ViewportHostPanelResult result = draw_viewport_host_panel(layout_state_, panes);
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

        update_viewport_from_surface(*viewport, scene, pane.surface, input, delta_seconds, visible_viewport_ids, frame_result);
    }
}

void EditorViewport::update_viewport_from_surface(
    ViewportInstance& viewport,
    const Scene& scene,
    const ViewportSurfaceResult& surface,
    const Input& input,
    float delta_seconds,
    std::vector<ViewportId>& visible_viewport_ids,
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

    if (surface.hovered || surface.clicked || (surface.hovered && is_viewport_movement_key_down(input))) {
        active_viewport_ = viewport.id();
    }

    if (surface.left_clicked && !surface.dragging) {
        frame_result.selection_requested = true;
        frame_result.picked_entity = pick_entity_from_surface(scene, viewport, surface);
    }

    const ViewportNavigationSnapshot navigation_snapshot = navigation_snapshot_from(input, surface.hovered);
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
    const std::vector<ViewportId>& visible_viewport_ids)
{
    for (ViewportId viewport_id : visible_viewport_ids) {
        ViewportInstance* viewport = find_viewport(viewport_id);
        if (viewport == nullptr) {
            continue;
        }

        std::optional<ViewportRenderRequest> request =
            viewport->try_begin_render_request(scene);
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

std::optional<SceneEntityId> EditorViewport::pick_entity_from_surface(
    const Scene& scene,
    const ViewportInstance& viewport,
    const ViewportSurfaceResult& surface) const
{
    if (!surface.click.has_value() || surface.image_bounds.width <= 0.0f || surface.image_bounds.height <= 0.0f) {
        return std::nullopt;
    }

    const Ray ray = viewport_ray_from_surface(viewport, surface);
    std::optional<RayHit> best_hit;

    SceneRenderSnapshot snapshot = build_scene_render_snapshot(scene);
    DebugDrawFrame debug_draw;
    append_light_gizmos(snapshot, debug_draw);
    consider_debug_pick_proxies(scene, ray, debug_draw.depth_tested, best_hit);
    consider_debug_pick_proxies(scene, ray, debug_draw.overlay, best_hit);

    for (const SceneEntity& entity : scene.entities()) {
        std::optional<float> hit_distance = intersect_mesh_entity(ray, entity);

        if (!hit_distance.has_value()) {
            continue;
        }

        if (!best_hit.has_value() || *hit_distance < best_hit->distance) {
            best_hit = RayHit {
                .entity_id = entity.id,
                .distance = *hit_distance,
            };
        }
    }

    if (!best_hit.has_value()) {
        return std::nullopt;
    }

    return best_hit->entity_id;
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
