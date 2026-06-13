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
    opacity,
    emission,
};

enum class PbrMaterialBlendMode : uint8_t {
    opaque,
    transparent,
    fade,
    masked,
    add,
    multiply,
    screen,
};

enum class PbrMaterialTransparencyMode : uint8_t {
    default_sorting,
    two_passes_one_side,
    two_passes_two_sides,
};

enum class PbrMaterialRefractionMode : uint8_t {
    none,
    cubemap,
    screen_space,
};

enum class PbrMaterialRefractionType : uint8_t {
    solid,
    thin,
};

enum class PbrMaterialAbsorptionMode : uint8_t {
    coefficients,
    transmittance_color,
};

enum class PbrMaterialCullingMode : uint8_t {
    none,
    front,
    back,
    front_and_back,
};

struct RenderCamera {
    Vec3 position { 3.0f, 2.1f, 3.5f };
    float yaw_degrees = -40.601295f;
    float pitch_degrees = -24.699650f;
    float vertical_fov_degrees = 52.0f;
    float near_plane = 0.1f;
    float far_plane = 1000.0f;
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
    LinearColor emission_color { 1.0f, 1.0f, 1.0f };
    TextureAssetReference emission_texture;
    float emission_intensity = 0.0f;
    float emission_exposure_weight = 1.0f;
    PbrMaterialBlendMode blend_mode = PbrMaterialBlendMode::opaque;
    PbrMaterialTransparencyMode transparency_mode = PbrMaterialTransparencyMode::default_sorting;
    PbrMaterialCullingMode culling = PbrMaterialCullingMode::back;
    bool double_sided = true;
    bool depth_write = true;
    bool depth_culling = true;
    bool color_write = true;
    bool alpha_to_coverage = true;
    bool transparent_shadow = false;
    float opacity = 1.0f;
    TextureAssetReference opacity_texture;
    float mask_threshold = 0.4f;
    PbrMaterialRefractionMode refraction_mode = PbrMaterialRefractionMode::none;
    PbrMaterialRefractionType refraction_type = PbrMaterialRefractionType::solid;
    float transmission = 0.0f;
    float ior = 1.5f;
    PbrMaterialAbsorptionMode absorption_mode = PbrMaterialAbsorptionMode::coefficients;
    Vec3 absorption { 0.0f, 0.0f, 0.0f };
    LinearColor transmittance_color { 1.0f, 1.0f, 1.0f };
    float transmittance_distance = 1.0f;
    float thickness = 0.5f;
    float micro_thickness = 0.0f;
    float dispersion = 0.0f;
};

enum class BloomBlendMode : uint8_t {
    add,
    interpolate,
};

enum class BloomQuality : uint8_t {
    low,
    medium,
    high,
};

struct BloomSettings {
    bool enabled = false;
    float strength = 0.1f;
    uint32_t resolution = 384;
    uint8_t levels = 6;
    BloomBlendMode blend_mode = BloomBlendMode::add;
    BloomQuality quality = BloomQuality::low;
    bool threshold = true;
    float highlight = 1000.0f;
};

struct PostProcessingSettings {
    BloomSettings bloom;
};

enum class EnvironmentLightingSource : uint8_t {
    built_in_default,
};

struct EnvironmentLightingSettings {
    EnvironmentLightingSource source = EnvironmentLightingSource::built_in_default;
    bool enabled = true;
    float intensity = 30000.0f;
    float rotation_degrees = 0.0f;
    bool show_skybox = false;
    LinearRgba fallback_color { 0.055f, 0.060f, 0.070f, 1.0f };
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

enum class PointLightFalloffMode : uint8_t {
    linear,
    inverse_squared,
};

struct PointLightSettings {
    LinearColor color { 1.0f, 0.94f, 0.86f };
    float intensity = 1000000.0f;
    float range = 10.0f;
    PointLightFalloffMode falloff_mode = PointLightFalloffMode::linear;
    PointLightShadowSettings shadow;
};

[[nodiscard]] LinearColor clamp_color(LinearColor color);
[[nodiscard]] SrgbColor clamp_color(SrgbColor color);
[[nodiscard]] LinearRgba clamp_color(LinearRgba color);
[[nodiscard]] const char* environment_lighting_source_name(EnvironmentLightingSource source) noexcept;
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
[[nodiscard]] BloomSettings clamp_bloom_settings(BloomSettings settings);
[[nodiscard]] PostProcessingSettings clamp_post_processing_settings(PostProcessingSettings settings);
[[nodiscard]] EnvironmentLightingSettings clamp_environment_lighting_settings(EnvironmentLightingSettings settings);
[[nodiscard]] uint32_t directional_shadow_quality_map_size(DirectionalShadowQuality quality);
[[nodiscard]] DirectionalShadowSettings clamp_directional_shadow_settings(DirectionalShadowSettings settings);
[[nodiscard]] DirectionalLightSettings clamp_directional_light_settings(DirectionalLightSettings settings);
[[nodiscard]] const char* point_light_falloff_mode_name(PointLightFalloffMode mode) noexcept;
[[nodiscard]] PointLightShadowSettings clamp_point_light_shadow_settings(PointLightShadowSettings settings);
[[nodiscard]] PointLightSettings clamp_point_light_settings(PointLightSettings settings);

} // namespace quader
