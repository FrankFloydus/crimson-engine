module;

#include <filament/Color.h>
#include <math/mat4.h>
#include <math/vec3.h>
#include <math/vec4.h>

#include <cmath>

export module quader.filament.scene_viewport_conversions;

export import quader.render.viewport_types;

export namespace quader {

[[nodiscard]] inline filament::math::float3 to_filament_color(LinearColor color)
{
    return filament::math::float3 { color.r, color.g, color.b };
}

[[nodiscard]] inline filament::math::float4 to_filament_color(LinearRgba color)
{
    return filament::math::float4 { color.r, color.g, color.b, color.a };
}

[[nodiscard]] inline filament::math::float3 to_filament_float3(Vec3 value)
{
    return filament::math::float3 { value.x, value.y, value.z };
}

[[nodiscard]] inline float squared_distance(Vec3 left, Vec3 right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

[[nodiscard]] inline float distance_between(Vec3 left, Vec3 right)
{
    return std::sqrt(squared_distance(left, right));
}

[[nodiscard]] inline filament::math::double3 to_filament_point(Vec3 position)
{
    return filament::math::double3 { position.x, position.y, position.z };
}

[[nodiscard]] inline filament::math::double3 to_filament_direction(Vec3 direction)
{
    return filament::math::double3 { direction.x, direction.y, direction.z };
}

[[nodiscard]] inline filament::math::double3 to_filament_up(Vec3 up)
{
    return filament::math::double3 { up.x, up.y, up.z };
}

[[nodiscard]] inline float degrees_to_radians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

[[nodiscard]] inline filament::math::mat4f scene_mesh_transform(const SceneRenderMesh& mesh)
{
    const filament::math::mat4f translation =
        filament::math::mat4f::translation(to_filament_float3(mesh.position));
    const filament::math::mat4f rotation_x =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.x), filament::math::float3 { 1.0f, 0.0f, 0.0f });
    const filament::math::mat4f rotation_y =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.y), filament::math::float3 { 0.0f, 1.0f, 0.0f });
    const filament::math::mat4f rotation_z =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.z), filament::math::float3 { 0.0f, 0.0f, 1.0f });
    const filament::math::mat4f scale =
        filament::math::mat4f::scaling(to_filament_float3(mesh.scale));

    return translation * rotation_z * rotation_y * rotation_x * scale;
}

} // namespace quader
