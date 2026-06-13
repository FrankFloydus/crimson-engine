module;

#include <algorithm>
#include <cmath>
#include <cstdint>

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

float nonnegative_finite_or(float value, float fallback)
{
    return std::max(finite_or(value, fallback), 0.0f);
}

Vec3 nonnegative_vec3_or(Vec3 value, Vec3 fallback)
{
    return Vec3 {
        nonnegative_finite_or(value.x, fallback.x),
        nonnegative_finite_or(value.y, fallback.y),
        nonnegative_finite_or(value.z, fallback.z),
    };
}

PbrMaterialBlendMode valid_or_default(PbrMaterialBlendMode mode)
{
    switch (mode) {
    case PbrMaterialBlendMode::opaque:
    case PbrMaterialBlendMode::transparent:
    case PbrMaterialBlendMode::fade:
    case PbrMaterialBlendMode::masked:
    case PbrMaterialBlendMode::add:
    case PbrMaterialBlendMode::multiply:
    case PbrMaterialBlendMode::screen:
        return mode;
    }

    return PbrMaterialBlendMode::opaque;
}

PbrMaterialTransparencyMode valid_or_default(PbrMaterialTransparencyMode mode)
{
    switch (mode) {
    case PbrMaterialTransparencyMode::default_sorting:
    case PbrMaterialTransparencyMode::two_passes_one_side:
    case PbrMaterialTransparencyMode::two_passes_two_sides:
        return mode;
    }

    return PbrMaterialTransparencyMode::default_sorting;
}

PbrMaterialRefractionMode valid_or_default(PbrMaterialRefractionMode mode)
{
    switch (mode) {
    case PbrMaterialRefractionMode::none:
    case PbrMaterialRefractionMode::cubemap:
    case PbrMaterialRefractionMode::screen_space:
        return mode;
    }

    return PbrMaterialRefractionMode::none;
}

PbrMaterialRefractionType valid_or_default(PbrMaterialRefractionType type)
{
    switch (type) {
    case PbrMaterialRefractionType::solid:
    case PbrMaterialRefractionType::thin:
        return type;
    }

    return PbrMaterialRefractionType::solid;
}

PbrMaterialAbsorptionMode valid_or_default(PbrMaterialAbsorptionMode mode)
{
    switch (mode) {
    case PbrMaterialAbsorptionMode::coefficients:
    case PbrMaterialAbsorptionMode::transmittance_color:
        return mode;
    }

    return PbrMaterialAbsorptionMode::coefficients;
}

PbrMaterialCullingMode valid_or_default(PbrMaterialCullingMode mode)
{
    switch (mode) {
    case PbrMaterialCullingMode::none:
    case PbrMaterialCullingMode::front:
    case PbrMaterialCullingMode::back:
    case PbrMaterialCullingMode::front_and_back:
        return mode;
    }

    return PbrMaterialCullingMode::back;
}

BloomBlendMode valid_or_default(BloomBlendMode mode)
{
    switch (mode) {
    case BloomBlendMode::add:
    case BloomBlendMode::interpolate:
        return mode;
    }

    return BloomBlendMode::add;
}

BloomQuality valid_or_default(BloomQuality quality)
{
    switch (quality) {
    case BloomQuality::low:
    case BloomQuality::medium:
    case BloomQuality::high:
        return quality;
    }

    return BloomQuality::low;
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

LinearRgba clamp_color(LinearRgba color)
{
    color.r = clamp_unit_channel(color.r);
    color.g = clamp_unit_channel(color.g);
    color.b = clamp_unit_channel(color.b);
    color.a = clamp_unit_channel(color.a);
    return color;
}

const char* environment_lighting_source_name(EnvironmentLightingSource source) noexcept
{
    switch (source) {
    case EnvironmentLightingSource::built_in_default:
        return "Built-in Default";
    }

    return "Built-in Default";
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

TextureAssetReference& material_texture_reference(PbrMaterialSettings& settings, PbrMaterialTextureSlot slot)
{
    switch (slot) {
    case PbrMaterialTextureSlot::albedo:
        return settings.albedo_texture;
    case PbrMaterialTextureSlot::roughness:
        return settings.roughness_texture;
    case PbrMaterialTextureSlot::metallic:
        return settings.metallic_texture;
    case PbrMaterialTextureSlot::opacity:
        return settings.opacity_texture;
    case PbrMaterialTextureSlot::emission:
        return settings.emission_texture;
    }

    return settings.albedo_texture;
}

const TextureAssetReference& material_texture_reference(const PbrMaterialSettings& settings, PbrMaterialTextureSlot slot)
{
    switch (slot) {
    case PbrMaterialTextureSlot::albedo:
        return settings.albedo_texture;
    case PbrMaterialTextureSlot::roughness:
        return settings.roughness_texture;
    case PbrMaterialTextureSlot::metallic:
        return settings.metallic_texture;
    case PbrMaterialTextureSlot::opacity:
        return settings.opacity_texture;
    case PbrMaterialTextureSlot::emission:
        return settings.emission_texture;
    }

    return settings.albedo_texture;
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
    settings.blend_mode = valid_or_default(settings.blend_mode);
    settings.transparency_mode = valid_or_default(settings.transparency_mode);
    settings.culling = valid_or_default(settings.culling);
    settings.refraction_mode = valid_or_default(settings.refraction_mode);
    settings.refraction_type = valid_or_default(settings.refraction_type);
    settings.absorption_mode = valid_or_default(settings.absorption_mode);
    settings.albedo = clamp_color(settings.albedo);
    settings.roughness = std::clamp(settings.roughness, 0.0f, 1.0f);
    settings.metallic = std::clamp(settings.metallic, 0.0f, 1.0f);
    settings.reflectance = std::clamp(settings.reflectance, 0.0f, 1.0f);
    settings.specular_factor = std::clamp(settings.specular_factor, 0.0f, 1.0f);
    settings.specular_color_factor = clamp_color(settings.specular_color_factor);
    settings.clear_coat = std::clamp(settings.clear_coat, 0.0f, 1.0f);
    settings.clear_coat_roughness = std::clamp(settings.clear_coat_roughness, 0.0f, 1.0f);
    settings.emission_color = clamp_color(settings.emission_color);
    settings.emission_intensity =
        std::clamp(finite_or(settings.emission_intensity, 0.0f), 0.0f, 100000000.0f);
    settings.emission_exposure_weight =
        std::clamp(finite_or(settings.emission_exposure_weight, 1.0f), 0.0f, 1.0f);
    settings.opacity = std::clamp(finite_or(settings.opacity, 1.0f), 0.0f, 1.0f);
    settings.mask_threshold = std::clamp(finite_or(settings.mask_threshold, 0.4f), 0.0f, 1.0f);
    settings.transmission = std::clamp(finite_or(settings.transmission, 0.0f), 0.0f, 1.0f);
    settings.ior = std::clamp(finite_or(settings.ior, 1.5f), 1.0f, 4.0f);
    settings.absorption = nonnegative_vec3_or(settings.absorption, Vec3 {});
    settings.transmittance_color = clamp_color(settings.transmittance_color);
    settings.transmittance_distance = std::max(finite_or(settings.transmittance_distance, 1.0f), 0.001f);
    settings.thickness = nonnegative_finite_or(settings.thickness, 0.5f);
    settings.micro_thickness = nonnegative_finite_or(settings.micro_thickness, 0.0f);
    settings.dispersion = std::clamp(finite_or(settings.dispersion, 0.0f), 0.0f, 1.0f);
    return settings;
}

BloomSettings clamp_bloom_settings(BloomSettings settings)
{
    const BloomSettings defaults;

    settings.strength = std::clamp(finite_or(settings.strength, defaults.strength), 0.0f, 1.0f);
    settings.resolution = std::clamp(settings.resolution, 8U, 4096U);
    settings.levels = static_cast<uint8_t>(std::clamp(static_cast<int>(settings.levels), 1, 11));
    settings.blend_mode = valid_or_default(settings.blend_mode);
    settings.quality = valid_or_default(settings.quality);
    settings.highlight = std::max(finite_or(settings.highlight, defaults.highlight), 10.0f);
    return settings;
}

PostProcessingSettings clamp_post_processing_settings(PostProcessingSettings settings)
{
    settings.bloom = clamp_bloom_settings(settings.bloom);
    return settings;
}

EnvironmentLightingSettings clamp_environment_lighting_settings(EnvironmentLightingSettings settings)
{
    const EnvironmentLightingSettings defaults;

    settings.intensity = std::clamp(finite_or(settings.intensity, defaults.intensity), 0.0f, 200000.0f);
    settings.rotation_degrees = normalize_yaw(settings.rotation_degrees);
    settings.fallback_color = clamp_color(settings.fallback_color);
    return settings;
}

uint32_t directional_shadow_quality_map_size(DirectionalShadowQuality quality)
{
    switch (quality) {
    case DirectionalShadowQuality::low:
        return 512;
    case DirectionalShadowQuality::medium:
        return 1024;
    case DirectionalShadowQuality::high:
        return 2048;
    case DirectionalShadowQuality::ultra:
        return 4096;
    }

    return 2048;
}

DirectionalShadowSettings clamp_directional_shadow_settings(DirectionalShadowSettings settings)
{
    const DirectionalShadowSettings defaults;

    settings.distance = std::max(finite_or(settings.distance, defaults.distance), 0.0f);
    settings.cascades = static_cast<uint8_t>(std::clamp(static_cast<int>(settings.cascades), 1, 4));
    settings.contact_distance = std::max(finite_or(settings.contact_distance, defaults.contact_distance), 0.0f);
    settings.contact_steps = static_cast<uint8_t>(std::clamp(static_cast<int>(settings.contact_steps), 1, 64));
    settings.constant_bias = std::max(finite_or(settings.constant_bias, defaults.constant_bias), 0.0f);
    settings.normal_bias = std::max(finite_or(settings.normal_bias, defaults.normal_bias), 0.0f);
    settings.polygon_offset_constant =
        std::max(finite_or(settings.polygon_offset_constant, defaults.polygon_offset_constant), 0.0f);
    settings.polygon_offset_slope =
        std::max(finite_or(settings.polygon_offset_slope, defaults.polygon_offset_slope), 0.0f);
    settings.near_hint = std::max(finite_or(settings.near_hint, defaults.near_hint), 0.0f);
    settings.far_hint = std::max(finite_or(settings.far_hint, defaults.far_hint), 0.0f);
    settings.vsm_blur_width =
        std::clamp(finite_or(settings.vsm_blur_width, defaults.vsm_blur_width), 0.0f, 125.0f);
    settings.vsm_light_bleed_reduction =
        std::clamp(finite_or(settings.vsm_light_bleed_reduction, defaults.vsm_light_bleed_reduction), 0.0f, 1.0f);
    settings.soft_shadow_bulb_radius =
        std::max(finite_or(settings.soft_shadow_bulb_radius, defaults.soft_shadow_bulb_radius), 0.0f);
    settings.penumbra_scale = std::max(finite_or(settings.penumbra_scale, defaults.penumbra_scale), 0.001f);
    settings.penumbra_ratio_scale =
        std::max(finite_or(settings.penumbra_ratio_scale, defaults.penumbra_ratio_scale), 1.0f);

    return settings;
}

DirectionalLightSettings clamp_directional_light_settings(DirectionalLightSettings settings)
{
    const DirectionalLightSettings defaults;

    settings.color = clamp_color(settings.color);
    settings.intensity = std::clamp(finite_or(settings.intensity, defaults.intensity), 0.0f, 200000.0f);
    settings.shadow = clamp_directional_shadow_settings(settings.shadow);
    return settings;
}

const char* point_light_falloff_mode_name(PointLightFalloffMode mode) noexcept
{
    switch (mode) {
    case PointLightFalloffMode::linear:
        return "Linear";
    case PointLightFalloffMode::inverse_squared:
        return "Inverse Squared";
    }

    return "Linear";
}

PointLightShadowSettings clamp_point_light_shadow_settings(PointLightShadowSettings settings)
{
    const PointLightShadowSettings defaults;

    settings.constant_bias = std::max(finite_or(settings.constant_bias, defaults.constant_bias), 0.0f);
    settings.normal_bias = std::max(finite_or(settings.normal_bias, defaults.normal_bias), 0.0f);
    return settings;
}

PointLightSettings clamp_point_light_settings(PointLightSettings settings)
{
    const PointLightSettings defaults;

    settings.color = clamp_color(settings.color);
    settings.intensity = std::clamp(finite_or(settings.intensity, defaults.intensity), 0.0f, 100000000.0f);
    settings.range = std::max(finite_or(settings.range, defaults.range), 0.0f);
    settings.shadow = clamp_point_light_shadow_settings(settings.shadow);
    return settings;
}

} // namespace quader
