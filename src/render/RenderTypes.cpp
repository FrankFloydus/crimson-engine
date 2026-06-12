module;

#include <algorithm>
#include <cmath>

module quader.render.types;

namespace quader {
namespace {

float clamp_unit_channel(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float finite_or(float value, float fallback)
{
    return std::isfinite(value) ? value : fallback;
}

float normalize_yaw(float yaw_degrees)
{
    yaw_degrees = finite_or(yaw_degrees, 0.0f);
    while (yaw_degrees > 180.0f) {
        yaw_degrees -= 360.0f;
    }
    while (yaw_degrees < -180.0f) {
        yaw_degrees += 360.0f;
    }
    return yaw_degrees;
}

} // namespace

LinearColor clamp_color(LinearColor color)
{
    color.r = clamp_unit_channel(color.r);
    color.g = clamp_unit_channel(color.g);
    color.b = clamp_unit_channel(color.b);
    return color;
}

SrgbColor clamp_color(SrgbColor color)
{
    color.r = clamp_unit_channel(color.r);
    color.g = clamp_unit_channel(color.g);
    color.b = clamp_unit_channel(color.b);
    return color;
}

float srgb_channel_to_linear(float channel)
{
    const float srgb = clamp_unit_channel(channel);
    if (srgb <= 0.04045f) {
        return srgb / 12.92f;
    }

    return static_cast<float>(std::pow((srgb + 0.055f) / 1.055f, 2.4f));
}

float linear_channel_to_srgb(float channel)
{
    const float linear = clamp_unit_channel(channel);
    if (linear <= 0.0031308f) {
        return linear * 12.92f;
    }

    return 1.055f * static_cast<float>(std::pow(linear, 1.0f / 2.4f)) - 0.055f;
}

LinearColor srgb_to_linear(SrgbColor color)
{
    color = clamp_color(color);
    return LinearColor {
        srgb_channel_to_linear(color.r),
        srgb_channel_to_linear(color.g),
        srgb_channel_to_linear(color.b),
    };
}

SrgbColor linear_to_srgb(LinearColor color)
{
    color = clamp_color(color);
    return SrgbColor {
        linear_channel_to_srgb(color.r),
        linear_channel_to_srgb(color.g),
        linear_channel_to_srgb(color.b),
    };
}

LinearRgba premultiplied_linear_rgba(LinearColor color, float alpha)
{
    color = clamp_color(color);
    const float clamped_alpha = clamp_unit_channel(alpha);
    return LinearRgba {
        color.r * clamped_alpha,
        color.g * clamped_alpha,
        color.b * clamped_alpha,
        clamped_alpha,
    };
}

RenderCamera clamp_render_camera(RenderCamera camera)
{
    const RenderCamera defaults;

    camera.position.x = finite_or(camera.position.x, defaults.position.x);
    camera.position.y = finite_or(camera.position.y, defaults.position.y);
    camera.position.z = finite_or(camera.position.z, defaults.position.z);
    camera.yaw_degrees = normalize_yaw(camera.yaw_degrees);
    camera.pitch_degrees = std::clamp(finite_or(camera.pitch_degrees, defaults.pitch_degrees), -89.0f, 89.0f);
    camera.vertical_fov_degrees =
        std::clamp(finite_or(camera.vertical_fov_degrees, defaults.vertical_fov_degrees), 1.0f, 120.0f);
    camera.near_plane = std::max(finite_or(camera.near_plane, defaults.near_plane), 0.001f);
    camera.far_plane = std::max(
        finite_or(camera.far_plane, defaults.far_plane),
        camera.near_plane + 0.001f);

    return camera;
}

PbrMaterialSettings clamp_pbr_material_settings(PbrMaterialSettings settings)
{
    settings.albedo = clamp_color(settings.albedo);
    settings.roughness = std::clamp(settings.roughness, 0.0f, 1.0f);
    settings.metallic = std::clamp(settings.metallic, 0.0f, 1.0f);
    settings.reflectance = std::clamp(settings.reflectance, 0.0f, 1.0f);
    settings.specular_factor = std::clamp(settings.specular_factor, 0.0f, 1.0f);
    settings.specular_color_factor = clamp_color(settings.specular_color_factor);
    settings.clear_coat = std::clamp(settings.clear_coat, 0.0f, 1.0f);
    settings.clear_coat_roughness = std::clamp(settings.clear_coat_roughness, 0.0f, 1.0f);
    return settings;
}

DirectionalLightSettings clamp_directional_light_settings(DirectionalLightSettings settings)
{
    settings.color = clamp_color(settings.color);
    settings.intensity = std::clamp(settings.intensity, 0.0f, 200000.0f);
    settings.pitch_degrees = std::clamp(settings.pitch_degrees, -89.0f, 89.0f);
    return settings;
}

} // namespace quader
