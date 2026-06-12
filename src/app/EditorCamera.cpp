module;

#include <algorithm>
#include <cmath>
#include <tracy/Tracy.hpp>

module quader.app.editor_camera;

namespace quader {
namespace {

constexpr Vec3 world_up { 0.0f, 1.0f, 0.0f };

Vec3 operator+(Vec3 left, Vec3 right)
{
    return { left.x + right.x, left.y + right.y, left.z + right.z };
}

Vec3 operator*(Vec3 value, float scale)
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

float squared_length(Vec3 value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

Vec3 cross(Vec3 left, Vec3 right)
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

Vec3 normalized_or_zero(Vec3 value)
{
    const float length_squared = squared_length(value);
    if (length_squared <= 0.000001f) {
        return {};
    }

    const float inverse_length = 1.0f / std::sqrt(length_squared);
    return value * inverse_length;
}

bool has_nonzero_vector(Vec3 value)
{
    return squared_length(value) > 0.000001f;
}

float finite_or_zero(float value)
{
    if (!std::isfinite(value)) {
        return 0.0f;
    }

    return value;
}

float normalized_delta_seconds(float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0f) {
        return 0.0f;
    }

    return std::min(delta_seconds, 0.1f);
}

float key_axis(bool positive_direction_down, bool negative_direction_down)
{
    return (positive_direction_down ? 1.0f : 0.0f) - (negative_direction_down ? 1.0f : 0.0f);
}

} // namespace

EditorCamera::EditorCamera()
    : camera_(clamp_render_camera(RenderCamera {}))
{
}

EditorCamera::EditorCamera(RenderCamera camera)
    : camera_(clamp_render_camera(camera))
{
}

void EditorCamera::set_camera(RenderCamera camera)
{
    camera_ = clamp_render_camera(camera);
}

RenderCamera EditorCamera::render_camera() const
{
    return camera_;
}

bool EditorCamera::update_from_navigation_input(
    const EditorCameraNavigationInput& navigation_input,
    float delta_seconds)
{
    ZoneScopedN("Editor camera update");

    bool changed = false;
    if (navigation_input.look_active) {
        const float look_delta_x = finite_or_zero(navigation_input.look_delta.x);
        const float look_delta_y = finite_or_zero(navigation_input.look_delta.y);
        if (look_delta_x != 0.0f || look_delta_y != 0.0f) {
            camera_.yaw_degrees += look_delta_x * look_sensitivity_degrees_per_pixel_;
            camera_.pitch_degrees = std::clamp(
                camera_.pitch_degrees - look_delta_y * look_sensitivity_degrees_per_pixel_,
                -89.0f,
                89.0f);
            changed = true;
        }
    }

    const float frame_seconds = normalized_delta_seconds(delta_seconds);
    const float move_forward_axis = key_axis(navigation_input.move_forward, navigation_input.move_backward);
    const float move_right_axis = key_axis(navigation_input.move_right, navigation_input.move_left);
    const Vec3 forward = normalized_direction_from_yaw_pitch(camera_.yaw_degrees, camera_.pitch_degrees);
    const Vec3 right = normalized_or_zero(cross(forward, world_up));
    const Vec3 movement = (forward * move_forward_axis) + (right * move_right_axis);

    if (frame_seconds > 0.0f && has_nonzero_vector(movement)) {
        const float movement_length = std::sqrt(squared_length(movement));
        const float movement_scale = std::min(movement_length, 1.0f);
        const float speed = movement_speed_ * (navigation_input.boost ? movement_boost_multiplier_ : 1.0f);
        camera_.position = camera_.position + (normalized_or_zero(movement) * (speed * frame_seconds * movement_scale));
        changed = true;
    }

    if (changed) {
        camera_ = clamp_render_camera(camera_);
    }

    return changed;
}

} // namespace quader
