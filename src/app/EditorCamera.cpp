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

Vec3 operator-(Vec3 left, Vec3 right)
{
    return { left.x - right.x, left.y - right.y, left.z - right.z };
}

Vec3 operator*(Vec3 value, float scale)
{
    return { value.x * scale, value.y * scale, value.z * scale };
}

Vec3& operator+=(Vec3& left, Vec3 right)
{
    left = left + right;
    return left;
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

float normalized_delta_seconds(float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0f) {
        return 0.0f;
    }

    return std::min(delta_seconds, 0.1f);
}

bool is_boosting(const Input& input)
{
    return input.is_key_down(AppKey::left_shift) || input.is_key_down(AppKey::right_shift);
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

bool EditorCamera::update_from_input(const Input& input, float delta_seconds)
{
    ZoneScopedN("Editor camera update");

    bool changed = false;
    if (input.is_mouse_button_down(AppMouseButton::right)) {
        const MouseDelta mouse_delta = input.mouse_delta();
        if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f) {
            camera_.yaw_degrees += mouse_delta.x * look_sensitivity_degrees_per_pixel_;
            camera_.pitch_degrees = std::clamp(
                camera_.pitch_degrees - mouse_delta.y * look_sensitivity_degrees_per_pixel_,
                -89.0f,
                89.0f);
            changed = true;
        }
    }

    const float frame_seconds = normalized_delta_seconds(delta_seconds);
    Vec3 movement {};
    const Vec3 forward = normalized_direction_from_yaw_pitch(camera_.yaw_degrees, camera_.pitch_degrees);
    const Vec3 right = normalized_or_zero(cross(forward, world_up));

    if (input.is_key_down(AppKey::w)) {
        movement += forward;
    }
    if (input.is_key_down(AppKey::s)) {
        movement = movement - forward;
    }
    if (input.is_key_down(AppKey::d)) {
        movement += right;
    }
    if (input.is_key_down(AppKey::a)) {
        movement = movement - right;
    }
    if (input.is_key_down(AppKey::e)) {
        movement += world_up;
    }
    if (input.is_key_down(AppKey::q)) {
        movement = movement - world_up;
    }

    if (frame_seconds > 0.0f && has_nonzero_vector(movement)) {
        const float speed = movement_speed_ * (is_boosting(input) ? movement_boost_multiplier_ : 1.0f);
        camera_.position += normalized_or_zero(movement) * (speed * frame_seconds);
        changed = true;
    }

    if (changed) {
        camera_ = clamp_render_camera(camera_);
    }

    return changed;
}

} // namespace quader
