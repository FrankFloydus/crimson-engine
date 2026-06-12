module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

module quader.app.viewport_instance;

namespace quader {
namespace {

constexpr Vec3 world_up { 0.0f, 1.0f, 0.0f };
constexpr Vec3 world_forward { 0.0f, 0.0f, -1.0f };
constexpr Vec3 top_view_up { 0.0f, 0.0f, -1.0f };

[[nodiscard]] Vec3 add(Vec3 left, Vec3 right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

[[nodiscard]] Vec3 scale(Vec3 value, float amount)
{
    return { value.x * amount, value.y * amount, value.z * amount };
}

[[nodiscard]] float finite_or(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

[[nodiscard]] float squared_length(Vec3 value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
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

    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return scale(value, inverse_length);
}

[[nodiscard]] float clamped_pitch(float pitch_degrees)
{
    return std::clamp(finite_or(pitch_degrees, 0.0f), -89.0f, 89.0f);
}

[[nodiscard]] ViewportCamera viewport_camera_from_render_camera(RenderCamera camera)
{
    camera = clamp_render_camera(camera);
    return ViewportCamera {
        .position = camera.position,
        .forward = normalized_direction_from_yaw_pitch(camera.yaw_degrees, camera.pitch_degrees),
        .up = world_up,
    };
}

[[nodiscard]] ViewportCamera orthographic_camera(
    ViewportCameraPreset preset,
    Vec3 target,
    float distance)
{
    switch (preset) {
    case ViewportCameraPreset::top:
        return {
            .position = add(target, Vec3 { 0.0f, distance, 0.0f }),
            .forward = { 0.0f, -1.0f, 0.0f },
            .up = top_view_up,
        };
    case ViewportCameraPreset::side:
        return {
            .position = add(target, Vec3 { distance, 0.0f, 0.0f }),
            .forward = { -1.0f, 0.0f, 0.0f },
            .up = world_up,
        };
    case ViewportCameraPreset::front:
        return {
            .position = add(target, Vec3 { 0.0f, 0.0f, distance }),
            .forward = world_forward,
            .up = world_up,
        };
    case ViewportCameraPreset::perspective:
        break;
    }

    return {};
}

[[nodiscard]] EditorCameraNavigationInput scene_navigation_input_from(
    const ViewportNavigationSnapshot& input,
    bool navigation_captured)
{
    EditorCameraNavigationInput navigation;
    const bool keyboard_navigation_active =
        (input.hovered || navigation_captured) &&
        !input.text_input_active;
    if (keyboard_navigation_active) {
        navigation.move_forward = input.move_forward;
        navigation.move_backward = input.move_backward;
        navigation.move_left = input.move_left;
        navigation.move_right = input.move_right;
        navigation.move_up = input.move_up;
        navigation.move_down = input.move_down;
        navigation.boost = input.boost;
    }

    navigation.look_active = navigation_captured;
    navigation.look_delta = navigation_captured ? input.mouse_delta : MouseDelta {};
    return navigation;
}

[[nodiscard]] SceneRenderSnapshot scene_render_snapshot_from(const Scene& scene)
{
    SceneRenderSnapshot snapshot;
    snapshot.revision = 1;

    for (const SceneEntity& entity : scene.entities()) {
        if (entity.mesh.has_value()) {
            snapshot.meshes.push_back(SceneRenderMesh {
                .mesh_kind = ViewportMeshKind::cube,
                .position = entity.transform.position,
                .rotation_degrees = entity.transform.rotation_degrees,
                .scale = entity.transform.scale,
                .material = entity.mesh->material,
            });
        }

        if (entity.directional_light.has_value()) {
            snapshot.directional_lights.push_back(SceneRenderDirectionalLight {
                .settings = entity.directional_light->settings,
            });
        }
    }

    return snapshot;
}

} // namespace

ViewportInstance ViewportInstance::scene_viewport(
    ViewportId id,
    std::string title,
    ViewportCameraPreset preset)
{
    return ViewportInstance(id, std::move(title), ViewportSubjectKind::scene, ViewportNavigationKind::scene_camera, preset);
}

ViewportInstance ViewportInstance::non_interactive_scene_viewport(
    ViewportId id,
    std::string title,
    ViewportCameraPreset preset)
{
    return ViewportInstance(id, std::move(title), ViewportSubjectKind::scene, ViewportNavigationKind::none, preset);
}

ViewportInstance ViewportInstance::preview_viewport(ViewportId id, std::string title)
{
    return ViewportInstance(
        id,
        std::move(title),
        ViewportSubjectKind::preview,
        ViewportNavigationKind::orbit_preview,
        ViewportCameraPreset::perspective);
}

ViewportInstance::ViewportInstance(
    ViewportId id,
    std::string title,
    ViewportSubjectKind subject_kind,
    ViewportNavigationKind navigation_kind,
    ViewportCameraPreset preset)
    : id_(id)
    , title_(std::move(title))
    , subject_kind_(subject_kind)
    , navigation_kind_(navigation_kind)
    , camera_preset_(preset)
{
}

ViewportId ViewportInstance::id() const noexcept { return id_; }
ViewportGeneration ViewportInstance::generation() const noexcept { return generation_; }
std::string_view ViewportInstance::title() const noexcept { return title_; }
ViewportSubjectKind ViewportInstance::subject_kind() const noexcept { return subject_kind_; }
ViewportCameraPreset ViewportInstance::camera_preset() const noexcept { return camera_preset_; }
RenderExtent ViewportInstance::requested_extent() const noexcept { return requested_extent_; }
RenderExtent ViewportInstance::displayed_extent() const noexcept { return displayed_extent_; }
bool ViewportInstance::is_dirty() const noexcept { return dirty_; }
bool ViewportInstance::request_pending() const noexcept { return request_pending_; }

void ViewportInstance::set_camera_preset(ViewportCameraPreset preset)
{
    if (camera_preset_ == preset) {
        return;
    }

    camera_preset_ = preset;
    mark_dirty();
}

void ViewportInstance::set_requested_extent(RenderExtent extent)
{
    if (!extent.is_valid() ||
        (requested_extent_.width == extent.width && requested_extent_.height == extent.height)) {
        return;
    }

    requested_extent_ = extent;
    mark_dirty();
}

void ViewportInstance::mark_dirty()
{
    if (request_pending_) {
        dirty_after_pending_ = true;
        return;
    }

    dirty_ = true;
}

bool ViewportInstance::update_navigation(
    const ViewportNavigationSnapshot& input,
    bool navigation_captured,
    float delta_seconds)
{
    const bool capture_active =
        navigation_captured &&
        input.window_has_keyboard_focus &&
        input.window_has_mouse_focus &&
        input.right_mouse_down &&
        can_capture_navigation();

    bool changed = false;
    switch (navigation_kind_) {
    case ViewportNavigationKind::scene_camera:
        changed = update_scene_navigation(input, capture_active, delta_seconds);
        break;
    case ViewportNavigationKind::orbit_preview:
        changed = update_preview_orbit_navigation(input, capture_active);
        break;
    case ViewportNavigationKind::none:
        changed = false;
        break;
    }

    if (changed) {
        mark_dirty();
    }

    return changed;
}

std::optional<ViewportRenderRequest> ViewportInstance::try_begin_render_request(
    const Scene& scene,
    const PbrMaterialSettings& preview_material,
    DirectionalLightSettings preview_light)
{
    if (!dirty_ || request_pending_ || !requested_extent_.is_valid()) {
        return std::nullopt;
    }

    ViewportRenderSubject subject = SceneViewportSubject { .snapshot = scene_render_snapshot_from(scene) };
    if (subject_kind_ == ViewportSubjectKind::preview) {
        subject = PreviewViewportSubject {
            .material = preview_material,
            .light = preview_light,
            .revision = generation_.value,
        };
    }

    const uint64_t request_serial = next_request_serial_++;
    request_pending_ = true;
    pending_request_serial_ = request_serial;
    dirty_ = false;

    return ViewportRenderRequest {
        .viewport_id = id_,
        .generation = generation_,
        .request_serial = request_serial,
        .subject = subject,
        .camera = current_camera(),
        .projection = current_projection(),
        .extent = requested_extent_,
    };
}

ViewportFrameAcceptance ViewportInstance::accept_frame(const ViewportRenderFrame& frame)
{
    if (frame.viewport_id != id_ ||
        frame.generation != generation_ ||
        frame.request_serial != pending_request_serial_) {
        return { .accepted = false, .stale = true };
    }

    request_pending_ = false;
    pending_request_serial_ = 0;

    if (frame.extent.width != requested_extent_.width || frame.extent.height != requested_extent_.height) {
        dirty_after_pending_ = false;
        dirty_ = true;
        return { .accepted = false, .stale = true };
    }

    if (frame.has_pixels()) {
        displayed_extent_ = frame.extent;
    } else {
        dirty_ = true;
    }

    if (dirty_after_pending_) {
        dirty_after_pending_ = false;
        dirty_ = true;
    }

    return { .accepted = frame.has_pixels(), .stale = false };
}

void ViewportInstance::fail_pending_request()
{
    request_pending_ = false;
    pending_request_serial_ = 0;
    dirty_ = true;
}

ViewportCamera ViewportInstance::current_camera() const
{
    if (subject_kind_ == ViewportSubjectKind::preview) {
        const Vec3 forward = normalized_direction_from_yaw_pitch(orbit_yaw_degrees_, orbit_pitch_degrees_);
        return ViewportCamera {
            .position = scale(forward, -orbit_distance_),
            .forward = forward,
            .up = world_up,
        };
    }

    if (camera_preset_ == ViewportCameraPreset::perspective) {
        return viewport_camera_from_render_camera(perspective_camera_.render_camera());
    }

    return orthographic_camera(camera_preset_, orthographic_target_, 10.0f);
}

ViewportProjection ViewportInstance::current_projection() const
{
    if (subject_kind_ == ViewportSubjectKind::preview || camera_preset_ == ViewportCameraPreset::perspective) {
        const RenderCamera render_camera = perspective_camera_.render_camera();
        return {
            .kind = ViewportProjectionKind::perspective,
            .vertical_fov_degrees = render_camera.vertical_fov_degrees,
            .orthographic_size = orthographic_size_,
            .near_plane = render_camera.near_plane,
            .far_plane = render_camera.far_plane,
        };
    }

    return {
        .kind = ViewportProjectionKind::orthographic,
        .vertical_fov_degrees = 52.0f,
        .orthographic_size = orthographic_size_,
        .near_plane = 0.01f,
        .far_plane = 100.0f,
    };
}

bool ViewportInstance::update_scene_navigation(
    const ViewportNavigationSnapshot& input,
    bool navigation_captured,
    float delta_seconds)
{
    if (camera_preset_ == ViewportCameraPreset::perspective) {
        return perspective_camera_.update_from_navigation_input(
            scene_navigation_input_from(input, navigation_captured),
            delta_seconds);
    }

    bool changed = false;
    if (input.hovered && input.mouse_wheel_delta.y != 0.0f) {
        const float scale_factor = input.mouse_wheel_delta.y > 0.0f ? 0.9f : 1.1f;
        orthographic_size_ = std::clamp(orthographic_size_ * scale_factor, 0.25f, 100.0f);
        changed = true;
    }

    if (navigation_captured && (input.mouse_delta.x != 0.0f || input.mouse_delta.y != 0.0f)) {
        const ViewportCamera camera = current_camera();
        const Vec3 right = normalized_or(cross(camera.forward, camera.up), { 1.0f, 0.0f, 0.0f });
        const float pan_scale = orthographic_size_ * 0.0025f;
        orthographic_target_ = add(
            orthographic_target_,
            add(scale(right, -input.mouse_delta.x * pan_scale), scale(camera.up, input.mouse_delta.y * pan_scale)));
        changed = true;
    }

    return changed;
}

bool ViewportInstance::update_preview_orbit_navigation(
    const ViewportNavigationSnapshot& input,
    bool navigation_captured)
{
    bool changed = false;
    if (navigation_captured) {
        if (input.mouse_delta.x != 0.0f || input.mouse_delta.y != 0.0f) {
            orbit_yaw_degrees_ += input.mouse_delta.x * 0.18f;
            orbit_pitch_degrees_ = clamped_pitch(orbit_pitch_degrees_ + input.mouse_delta.y * 0.18f);
            changed = true;
        }
    }

    if (input.hovered && input.mouse_wheel_delta.y != 0.0f) {
        const float scale_factor = input.mouse_wheel_delta.y > 0.0f ? 0.9f : 1.1f;
        orbit_distance_ = std::clamp(orbit_distance_ * scale_factor, 1.0f, 25.0f);
        changed = true;
    }

    return changed;
}

bool ViewportInstance::can_capture_navigation() const noexcept
{
    switch (navigation_kind_) {
    case ViewportNavigationKind::scene_camera:
        return true;
    case ViewportNavigationKind::orbit_preview:
        return true;
    case ViewportNavigationKind::none:
        return false;
    }

    return false;
}

bool ViewportInstance::should_start_navigation_capture(const ViewportNavigationSnapshot& input) const noexcept
{
    if (!input.window_has_keyboard_focus || !input.window_has_mouse_focus) {
        return false;
    }

    if (!can_capture_navigation()) {
        return false;
    }

    return input.right_mouse_pressed && input.hovered;
}

} // namespace quader
