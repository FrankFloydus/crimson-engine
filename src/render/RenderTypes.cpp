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
