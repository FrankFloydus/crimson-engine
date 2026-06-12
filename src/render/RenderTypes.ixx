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

struct RenderViewportFrame {
    RenderExtent extent;
    std::vector<uint8_t> rgba;

    [[nodiscard]] bool has_pixels() const
    {
        return extent.is_valid() && rgba.size() == static_cast<std::size_t>(extent.width) * extent.height * 4;
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
    LinearColor albedo { 1.0f, 0.0f, 0.0f };
    TextureAssetReference albedo_texture;
    float roughness = 0.18f;
    TextureAssetReference roughness_texture;
    float metallic = 0.0f;
    TextureAssetReference metallic_texture;
    float reflectance = 0.8f;
    float specular_factor = 1.0f;
    LinearColor specular_color_factor { 1.0f, 1.0f, 1.0f };
    float clear_coat = 0.0f;
    float clear_coat_roughness = 0.0f;
};

struct DirectionalLightSettings {
    LinearColor color { 1.0f, 0.94f, 0.86f };
    float intensity = 65000.0f;
    float yaw_degrees = 140.0f;
    float pitch_degrees = -24.0f;
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
[[nodiscard]] DirectionalLightSettings clamp_directional_light_settings(DirectionalLightSettings settings);

} // namespace quader
