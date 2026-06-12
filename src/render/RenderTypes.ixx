module;

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module quader.render.types;

export import quader.math.types;

export namespace quader {

struct RenderExtent {
    uint32_t width = 0;
    uint32_t height = 0;

    [[nodiscard]] bool is_valid() const
    {
        return width > 0 && height > 0;
    }
};

struct LinearColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct SrgbColor {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct LinearRgba {
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float a = 1.0f;
};

struct TextureAssetReference {
    std::string source_path;

    [[nodiscard]] bool has_source() const
    {
        return !source_path.empty();
    }

    void clear()
    {
        source_path.clear();
    }
};

enum class PbrMaterialTextureSlot : uint8_t {
    albedo,
    roughness,
    metallic,
};

struct RenderCamera {
    Vec3 position { 3.0f, 2.1f, 3.5f };
    float yaw_degrees = -40.601295f;
    float pitch_degrees = -24.699650f;
    float vertical_fov_degrees = 52.0f;
    float near_plane = 0.1f;
    float far_plane = 100.0f;
};

struct PbrMaterialSettings {
    LinearColor albedo { 1.0f, 1.0f, 1.0f };
    TextureAssetReference albedo_texture;
    float roughness = 1.0f;
    TextureAssetReference roughness_texture;
    float metallic = 0.0f;
    TextureAssetReference metallic_texture;
    float reflectance = 0.8f;
    float specular_factor = 1.0f;
    LinearColor specular_color_factor { 1.0f, 1.0f, 1.0f };
    float clear_coat = 0.0f;
    float clear_coat_roughness = 0.0f;
};

enum class DirectionalShadowMode : uint8_t {
    crisp,
    soft,
    vsm_transparent,
};

enum class DirectionalShadowQuality : uint8_t {
    low,
    medium,
    high,
    ultra,
};

struct DirectionalShadowSettings {
    bool enabled = true;
    DirectionalShadowMode mode = DirectionalShadowMode::crisp;
    DirectionalShadowQuality quality = DirectionalShadowQuality::high;
    float distance = 150.0f;
    uint8_t cascades = 3;
    bool stable = false;
    bool contact_shadows = true;
    float contact_distance = 0.3f;
    uint8_t contact_steps = 8;
    float constant_bias = 0.001f;
    float normal_bias = 1.0f;
    float polygon_offset_constant = 0.5f;
    float polygon_offset_slope = 2.0f;
    float near_hint = 1.0f;
    float far_hint = 100.0f;
    bool lispsm = true;
    float vsm_blur_width = 0.0f;
    bool vsm_elvsm = false;
    bool vsm_high_precision = false;
    float vsm_light_bleed_reduction = 0.15f;
    float soft_shadow_bulb_radius = 0.02f;
    float penumbra_scale = 1.0f;
    float penumbra_ratio_scale = 1.0f;
};

struct DirectionalLightSettings {
    LinearColor color { 1.0f, 0.94f, 0.86f };
    float intensity = 65000.0f;
    DirectionalShadowSettings shadow;
};

struct PointLightShadowSettings {
    bool enabled = true;
    DirectionalShadowQuality quality = DirectionalShadowQuality::medium;
    float constant_bias = 0.001f;
    float normal_bias = 1.0f;
};

struct PointLightSettings {
    LinearColor color { 1.0f, 0.94f, 0.86f };
    float intensity = 10000.0f;
    float range = 10.0f;
    float falloff = 1.0f;
    PointLightShadowSettings shadow;
};

[[nodiscard]] LinearColor clamp_color(LinearColor color);
[[nodiscard]] SrgbColor clamp_color(SrgbColor color);
[[nodiscard]] float srgb_channel_to_linear(float channel);
[[nodiscard]] float linear_channel_to_srgb(float channel);
[[nodiscard]] LinearColor srgb_to_linear(SrgbColor color);
[[nodiscard]] SrgbColor linear_to_srgb(LinearColor color);
[[nodiscard]] LinearRgba premultiplied_linear_rgba(LinearColor color, float alpha);
[[nodiscard]] TextureAssetReference& material_texture_reference(
    PbrMaterialSettings& settings,
    PbrMaterialTextureSlot slot);
[[nodiscard]] const TextureAssetReference& material_texture_reference(
    const PbrMaterialSettings& settings,
    PbrMaterialTextureSlot slot);
[[nodiscard]] RenderCamera clamp_render_camera(RenderCamera camera);
[[nodiscard]] PbrMaterialSettings clamp_pbr_material_settings(PbrMaterialSettings settings);
[[nodiscard]] uint32_t directional_shadow_quality_map_size(DirectionalShadowQuality quality);
[[nodiscard]] DirectionalShadowSettings clamp_directional_shadow_settings(DirectionalShadowSettings settings);
[[nodiscard]] DirectionalLightSettings clamp_directional_light_settings(DirectionalLightSettings settings);
[[nodiscard]] PointLightShadowSettings clamp_point_light_shadow_settings(PointLightShadowSettings settings);
[[nodiscard]] PointLightSettings clamp_point_light_settings(PointLightSettings settings);

} // namespace quader
