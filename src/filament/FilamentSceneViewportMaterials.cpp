module;

#include <backend/PixelBufferDescriptor.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/Material.h>
#include <filament/MaterialEnums.h>
#include <filament/MaterialInstance.h>
#include <filament/Texture.h>
#include <filament/TextureSampler.h>

#include "diagnostics/LogMacros.h"
#include "generated/QuaderLitPbrMaterial.inc"
#include "generated/QuaderLitPbrOpaqueTransparentShadowMaterial.inc"
#include "generated/QuaderLitPbrTransparentMaterial.inc"
#include "generated/QuaderLitPbrTransparentShadowMaterial.inc"
#include "generated/QuaderLitPbrFadeMaterial.inc"
#include "generated/QuaderLitPbrFadeShadowMaterial.inc"
#include "generated/QuaderLitPbrMaskedMaterial.inc"
#include "generated/QuaderLitPbrMaskedNoAlphaToCoverageMaterial.inc"
#include "generated/QuaderLitPbrMaskedShadowMaterial.inc"
#include "generated/QuaderLitPbrMaskedNoAlphaToCoverageShadowMaterial.inc"
#include "generated/QuaderLitPbrAddMaterial.inc"
#include "generated/QuaderLitPbrAddShadowMaterial.inc"
#include "generated/QuaderLitPbrMultiplyMaterial.inc"
#include "generated/QuaderLitPbrMultiplyShadowMaterial.inc"
#include "generated/QuaderLitPbrScreenMaterial.inc"
#include "generated/QuaderLitPbrScreenShadowMaterial.inc"
#include "generated/QuaderLitPbrRefractCubemapSolidMaterial.inc"
#include "generated/QuaderLitPbrRefractCubemapSolidShadowMaterial.inc"
#include "generated/QuaderLitPbrRefractCubemapThinMaterial.inc"
#include "generated/QuaderLitPbrRefractCubemapThinShadowMaterial.inc"
#include "generated/QuaderLitPbrRefractScreenSpaceSolidMaterial.inc"
#include "generated/QuaderLitPbrRefractScreenSpaceSolidShadowMaterial.inc"
#include "generated/QuaderLitPbrRefractScreenSpaceThinMaterial.inc"
#include "generated/QuaderLitPbrRefractScreenSpaceThinShadowMaterial.inc"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

module quader.filament.scene_viewport_materials;

import quader.assets.image;
import quader.diagnostics.logger;
import quader.filament.buffer_upload;
import quader.filament.scene_viewport_conversions;
import quader.render.viewport_types;

namespace quader {
namespace {

enum class PreviewTextureEncoding : uint8_t {
    srgb_rgba,
    linear_red,
};

struct MaterialTextureBinding {
    filament::Texture* texture = nullptr;
    bool use_source_texture = false;
};

struct CachedMaterialTexture {
    filament::Texture* texture = nullptr;
};

[[nodiscard]] const char* texture_slot_name(PbrMaterialTextureSlot slot)
{
    switch (slot) {
    case PbrMaterialTextureSlot::albedo:
        return "albedo";
    case PbrMaterialTextureSlot::roughness:
        return "roughness";
    case PbrMaterialTextureSlot::metallic:
        return "metallic";
    case PbrMaterialTextureSlot::opacity:
        return "opacity";
    case PbrMaterialTextureSlot::emission:
        return "emission";
    }

    return "unknown";
}

[[nodiscard]] bool pbr_material_variant_uses_mask_threshold(FilamentPbrMaterialVariant variant)
{
    switch (variant) {
    case FilamentPbrMaterialVariant::masked:
    case FilamentPbrMaterialVariant::masked_no_alpha_to_coverage:
    case FilamentPbrMaterialVariant::masked_shadow:
    case FilamentPbrMaterialVariant::masked_no_alpha_to_coverage_shadow:
        return true;
    case FilamentPbrMaterialVariant::opaque:
    case FilamentPbrMaterialVariant::opaque_transparent_shadow:
    case FilamentPbrMaterialVariant::transparent:
    case FilamentPbrMaterialVariant::transparent_shadow:
    case FilamentPbrMaterialVariant::fade:
    case FilamentPbrMaterialVariant::fade_shadow:
    case FilamentPbrMaterialVariant::add:
    case FilamentPbrMaterialVariant::add_shadow:
    case FilamentPbrMaterialVariant::multiply:
    case FilamentPbrMaterialVariant::multiply_shadow:
    case FilamentPbrMaterialVariant::screen:
    case FilamentPbrMaterialVariant::screen_shadow:
    case FilamentPbrMaterialVariant::refract_cubemap_solid:
    case FilamentPbrMaterialVariant::refract_cubemap_solid_shadow:
    case FilamentPbrMaterialVariant::refract_cubemap_thin:
    case FilamentPbrMaterialVariant::refract_cubemap_thin_shadow:
    case FilamentPbrMaterialVariant::refract_screen_space_solid:
    case FilamentPbrMaterialVariant::refract_screen_space_solid_shadow:
    case FilamentPbrMaterialVariant::refract_screen_space_thin:
    case FilamentPbrMaterialVariant::refract_screen_space_thin_shadow:
    case FilamentPbrMaterialVariant::count:
        return false;
    }

    return false;
}

[[nodiscard]] bool pbr_material_variant_uses_transparency_mode(FilamentPbrMaterialVariant variant)
{
    switch (variant) {
    case FilamentPbrMaterialVariant::transparent:
    case FilamentPbrMaterialVariant::transparent_shadow:
    case FilamentPbrMaterialVariant::fade:
    case FilamentPbrMaterialVariant::fade_shadow:
    case FilamentPbrMaterialVariant::add:
    case FilamentPbrMaterialVariant::add_shadow:
    case FilamentPbrMaterialVariant::multiply:
    case FilamentPbrMaterialVariant::multiply_shadow:
    case FilamentPbrMaterialVariant::screen:
    case FilamentPbrMaterialVariant::screen_shadow:
        return true;
    case FilamentPbrMaterialVariant::opaque:
    case FilamentPbrMaterialVariant::opaque_transparent_shadow:
    case FilamentPbrMaterialVariant::masked:
    case FilamentPbrMaterialVariant::masked_no_alpha_to_coverage:
    case FilamentPbrMaterialVariant::masked_shadow:
    case FilamentPbrMaterialVariant::masked_no_alpha_to_coverage_shadow:
    case FilamentPbrMaterialVariant::refract_cubemap_solid:
    case FilamentPbrMaterialVariant::refract_cubemap_solid_shadow:
    case FilamentPbrMaterialVariant::refract_cubemap_thin:
    case FilamentPbrMaterialVariant::refract_cubemap_thin_shadow:
    case FilamentPbrMaterialVariant::refract_screen_space_solid:
    case FilamentPbrMaterialVariant::refract_screen_space_solid_shadow:
    case FilamentPbrMaterialVariant::refract_screen_space_thin:
    case FilamentPbrMaterialVariant::refract_screen_space_thin_shadow:
    case FilamentPbrMaterialVariant::count:
        return false;
    }

    return false;
}

[[nodiscard]] filament::TransparencyMode filament_transparency_mode(PbrMaterialTransparencyMode mode)
{
    switch (mode) {
    case PbrMaterialTransparencyMode::default_sorting:
        return filament::TransparencyMode::DEFAULT;
    case PbrMaterialTransparencyMode::two_passes_one_side:
        return filament::TransparencyMode::TWO_PASSES_ONE_SIDE;
    case PbrMaterialTransparencyMode::two_passes_two_sides:
        return filament::TransparencyMode::TWO_PASSES_TWO_SIDES;
    }

    return filament::TransparencyMode::DEFAULT;
}

[[nodiscard]] filament::MaterialInstance::CullingMode filament_culling_mode(PbrMaterialCullingMode mode)
{
    switch (mode) {
    case PbrMaterialCullingMode::none:
        return filament::MaterialInstance::CullingMode::NONE;
    case PbrMaterialCullingMode::front:
        return filament::MaterialInstance::CullingMode::FRONT;
    case PbrMaterialCullingMode::back:
        return filament::MaterialInstance::CullingMode::BACK;
    case PbrMaterialCullingMode::front_and_back:
        return filament::MaterialInstance::CullingMode::FRONT_AND_BACK;
    }

    return filament::MaterialInstance::CullingMode::BACK;
}

[[nodiscard]] filament::math::float3 filament_absorption(PbrMaterialSettings settings)
{
    settings = clamp_pbr_material_settings(settings);
    if (settings.absorption_mode == PbrMaterialAbsorptionMode::transmittance_color) {
        return filament::Color::absorptionAtDistance(
            to_filament_color(settings.transmittance_color),
            settings.transmittance_distance);
    }

    return to_filament_float3(settings.absorption);
}

struct EmbeddedMaterialPackage {
    const unsigned char* bytes = nullptr;
    unsigned int size = 0;
    const char* description = "";
};

[[nodiscard]] EmbeddedMaterialPackage embedded_material_package(FilamentPbrMaterialVariant variant)
{
    switch (variant) {
    case FilamentPbrMaterialVariant::opaque:
        return { filament_assets::quader_lit_pbr_material, filament_assets::quader_lit_pbr_material_size, "opaque" };
    case FilamentPbrMaterialVariant::opaque_transparent_shadow:
        return {
            filament_assets::quader_lit_pbr_opaque_transparent_shadow_material,
            filament_assets::quader_lit_pbr_opaque_transparent_shadow_material_size,
            "opaque transparent-shadow",
        };
    case FilamentPbrMaterialVariant::transparent:
        return {
            filament_assets::quader_lit_pbr_transparent_material,
            filament_assets::quader_lit_pbr_transparent_material_size,
            "transparent",
        };
    case FilamentPbrMaterialVariant::transparent_shadow:
        return {
            filament_assets::quader_lit_pbr_transparent_shadow_material,
            filament_assets::quader_lit_pbr_transparent_shadow_material_size,
            "transparent shadow",
        };
    case FilamentPbrMaterialVariant::fade:
        return { filament_assets::quader_lit_pbr_fade_material, filament_assets::quader_lit_pbr_fade_material_size, "fade" };
    case FilamentPbrMaterialVariant::fade_shadow:
        return {
            filament_assets::quader_lit_pbr_fade_shadow_material,
            filament_assets::quader_lit_pbr_fade_shadow_material_size,
            "fade shadow",
        };
    case FilamentPbrMaterialVariant::masked:
        return { filament_assets::quader_lit_pbr_masked_material, filament_assets::quader_lit_pbr_masked_material_size, "masked" };
    case FilamentPbrMaterialVariant::masked_no_alpha_to_coverage:
        return {
            filament_assets::quader_lit_pbr_masked_no_alpha_to_coverage_material,
            filament_assets::quader_lit_pbr_masked_no_alpha_to_coverage_material_size,
            "masked no-alpha-to-coverage",
        };
    case FilamentPbrMaterialVariant::masked_shadow:
        return {
            filament_assets::quader_lit_pbr_masked_shadow_material,
            filament_assets::quader_lit_pbr_masked_shadow_material_size,
            "masked shadow",
        };
    case FilamentPbrMaterialVariant::masked_no_alpha_to_coverage_shadow:
        return {
            filament_assets::quader_lit_pbr_masked_no_alpha_to_coverage_shadow_material,
            filament_assets::quader_lit_pbr_masked_no_alpha_to_coverage_shadow_material_size,
            "masked no-alpha-to-coverage shadow",
        };
    case FilamentPbrMaterialVariant::add:
        return { filament_assets::quader_lit_pbr_add_material, filament_assets::quader_lit_pbr_add_material_size, "add" };
    case FilamentPbrMaterialVariant::add_shadow:
        return {
            filament_assets::quader_lit_pbr_add_shadow_material,
            filament_assets::quader_lit_pbr_add_shadow_material_size,
            "add shadow",
        };
    case FilamentPbrMaterialVariant::multiply:
        return {
            filament_assets::quader_lit_pbr_multiply_material,
            filament_assets::quader_lit_pbr_multiply_material_size,
            "multiply",
        };
    case FilamentPbrMaterialVariant::multiply_shadow:
        return {
            filament_assets::quader_lit_pbr_multiply_shadow_material,
            filament_assets::quader_lit_pbr_multiply_shadow_material_size,
            "multiply shadow",
        };
    case FilamentPbrMaterialVariant::screen:
        return { filament_assets::quader_lit_pbr_screen_material, filament_assets::quader_lit_pbr_screen_material_size, "screen" };
    case FilamentPbrMaterialVariant::screen_shadow:
        return {
            filament_assets::quader_lit_pbr_screen_shadow_material,
            filament_assets::quader_lit_pbr_screen_shadow_material_size,
            "screen shadow",
        };
    case FilamentPbrMaterialVariant::refract_cubemap_solid:
        return {
            filament_assets::quader_lit_pbr_refract_cubemap_solid_material,
            filament_assets::quader_lit_pbr_refract_cubemap_solid_material_size,
            "cubemap solid refraction",
        };
    case FilamentPbrMaterialVariant::refract_cubemap_solid_shadow:
        return {
            filament_assets::quader_lit_pbr_refract_cubemap_solid_shadow_material,
            filament_assets::quader_lit_pbr_refract_cubemap_solid_shadow_material_size,
            "cubemap solid refraction shadow",
        };
    case FilamentPbrMaterialVariant::refract_cubemap_thin:
        return {
            filament_assets::quader_lit_pbr_refract_cubemap_thin_material,
            filament_assets::quader_lit_pbr_refract_cubemap_thin_material_size,
            "cubemap thin refraction",
        };
    case FilamentPbrMaterialVariant::refract_cubemap_thin_shadow:
        return {
            filament_assets::quader_lit_pbr_refract_cubemap_thin_shadow_material,
            filament_assets::quader_lit_pbr_refract_cubemap_thin_shadow_material_size,
            "cubemap thin refraction shadow",
        };
    case FilamentPbrMaterialVariant::refract_screen_space_solid:
        return {
            filament_assets::quader_lit_pbr_refract_screen_space_solid_material,
            filament_assets::quader_lit_pbr_refract_screen_space_solid_material_size,
            "screen-space solid refraction",
        };
    case FilamentPbrMaterialVariant::refract_screen_space_solid_shadow:
        return {
            filament_assets::quader_lit_pbr_refract_screen_space_solid_shadow_material,
            filament_assets::quader_lit_pbr_refract_screen_space_solid_shadow_material_size,
            "screen-space solid refraction shadow",
        };
    case FilamentPbrMaterialVariant::refract_screen_space_thin:
        return {
            filament_assets::quader_lit_pbr_refract_screen_space_thin_material,
            filament_assets::quader_lit_pbr_refract_screen_space_thin_material_size,
            "screen-space thin refraction",
        };
    case FilamentPbrMaterialVariant::refract_screen_space_thin_shadow:
        return {
            filament_assets::quader_lit_pbr_refract_screen_space_thin_shadow_material,
            filament_assets::quader_lit_pbr_refract_screen_space_thin_shadow_material_size,
            "screen-space thin refraction shadow",
        };
    case FilamentPbrMaterialVariant::count:
        break;
    }

    return { filament_assets::quader_lit_pbr_material, filament_assets::quader_lit_pbr_material_size, "opaque" };
}

[[nodiscard]] std::string material_texture_cache_key(
    std::string_view source_path,
    PbrMaterialTextureSlot slot,
    PreviewTextureEncoding encoding)
{
    return std::to_string(static_cast<uint32_t>(slot)) + "|" +
        std::to_string(static_cast<uint32_t>(encoding)) + "|" +
        std::string(source_path);
}

} // namespace

bool filament_pbr_material_uses_screen_space_refraction(const PbrMaterialSettings& settings)
{
    return settings.refraction_mode == PbrMaterialRefractionMode::screen_space;
}

FilamentPbrMaterialVariant filament_pbr_material_variant_for(PbrMaterialSettings settings)
{
    settings = clamp_pbr_material_settings(settings);
    const bool transparent_shadow = settings.transparent_shadow;

    switch (settings.refraction_mode) {
    case PbrMaterialRefractionMode::cubemap:
        if (settings.refraction_type == PbrMaterialRefractionType::thin) {
            return transparent_shadow
                ? FilamentPbrMaterialVariant::refract_cubemap_thin_shadow
                : FilamentPbrMaterialVariant::refract_cubemap_thin;
        }
        return transparent_shadow
            ? FilamentPbrMaterialVariant::refract_cubemap_solid_shadow
            : FilamentPbrMaterialVariant::refract_cubemap_solid;
    case PbrMaterialRefractionMode::screen_space:
        if (settings.refraction_type == PbrMaterialRefractionType::thin) {
            return transparent_shadow
                ? FilamentPbrMaterialVariant::refract_screen_space_thin_shadow
                : FilamentPbrMaterialVariant::refract_screen_space_thin;
        }
        return transparent_shadow
            ? FilamentPbrMaterialVariant::refract_screen_space_solid_shadow
            : FilamentPbrMaterialVariant::refract_screen_space_solid;
    case PbrMaterialRefractionMode::none:
        break;
    }

    switch (settings.blend_mode) {
    case PbrMaterialBlendMode::opaque:
        return transparent_shadow ? FilamentPbrMaterialVariant::opaque_transparent_shadow : FilamentPbrMaterialVariant::opaque;
    case PbrMaterialBlendMode::transparent:
        return transparent_shadow
            ? FilamentPbrMaterialVariant::transparent_shadow
            : FilamentPbrMaterialVariant::transparent;
    case PbrMaterialBlendMode::fade:
        return transparent_shadow ? FilamentPbrMaterialVariant::fade_shadow : FilamentPbrMaterialVariant::fade;
    case PbrMaterialBlendMode::masked:
        if (settings.alpha_to_coverage) {
            return transparent_shadow
                ? FilamentPbrMaterialVariant::masked_shadow
                : FilamentPbrMaterialVariant::masked;
        }
        return transparent_shadow
            ? FilamentPbrMaterialVariant::masked_no_alpha_to_coverage_shadow
            : FilamentPbrMaterialVariant::masked_no_alpha_to_coverage;
    case PbrMaterialBlendMode::add:
        return transparent_shadow ? FilamentPbrMaterialVariant::add_shadow : FilamentPbrMaterialVariant::add;
    case PbrMaterialBlendMode::multiply:
        return transparent_shadow
            ? FilamentPbrMaterialVariant::multiply_shadow
            : FilamentPbrMaterialVariant::multiply;
    case PbrMaterialBlendMode::screen:
        return transparent_shadow
            ? FilamentPbrMaterialVariant::screen_shadow
            : FilamentPbrMaterialVariant::screen;
    }

    return FilamentPbrMaterialVariant::opaque;
}

struct FilamentPbrMaterialResources::Resources {
    explicit Resources(filament::Engine& engine_ref)
        : engine(engine_ref)
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        destroy();
    }

    [[nodiscard]] filament::Material* ensure_material(FilamentPbrMaterialVariant variant)
    {
        const size_t index = static_cast<size_t>(variant);
        if (index >= materials.size()) {
            return ensure_material(FilamentPbrMaterialVariant::opaque);
        }

        if (materials[index] != nullptr) {
            return materials[index];
        }

        const EmbeddedMaterialPackage package = embedded_material_package(variant);
        materials[index] = filament::Material::Builder()
            .package(package.bytes, package.size)
            .build(engine);
        return require_filament_allocation(materials[index], package.description);
    }

    [[nodiscard]] filament::MaterialInstance* create_mesh_material_instance(
        ViewportMeshKind mesh_kind,
        FilamentPbrMaterialVariant variant)
    {
        filament::Material* material = ensure_material(variant);
        filament::MaterialInstance* material_instance = material->createInstance(
            mesh_kind == ViewportMeshKind::plane ? "Quader plane material" : "Quader cube material");
        return require_filament_allocation(material_instance, "viewport mesh material instance");
    }

    void apply_settings(
        filament::MaterialInstance& instance,
        FilamentPbrMaterialVariant variant,
        PbrMaterialSettings settings)
    {
        settings = clamp_pbr_material_settings(settings);
        ensure_fallback_textures();

        const MaterialTextureBinding albedo_texture = resolve_texture(
            settings.albedo_texture,
            PbrMaterialTextureSlot::albedo,
            PreviewTextureEncoding::srgb_rgba,
            fallback_albedo_texture);
        const MaterialTextureBinding roughness_texture = resolve_texture(
            settings.roughness_texture,
            PbrMaterialTextureSlot::roughness,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);
        const MaterialTextureBinding metallic_texture = resolve_texture(
            settings.metallic_texture,
            PbrMaterialTextureSlot::metallic,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);
        const MaterialTextureBinding opacity_texture = resolve_texture(
            settings.opacity_texture,
            PbrMaterialTextureSlot::opacity,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);
        const MaterialTextureBinding emission_texture = resolve_texture(
            settings.emission_texture,
            PbrMaterialTextureSlot::emission,
            PreviewTextureEncoding::srgb_rgba,
            fallback_albedo_texture);

        instance.setParameter(
            "albedo",
            filament::RgbaType::LINEAR,
            filament::math::float4 { settings.albedo.r, settings.albedo.g, settings.albedo.b, 1.0f });
        instance.setParameter("albedoTexture", albedo_texture.texture, texture_sampler);
        instance.setParameter("useAlbedoTexture", albedo_texture.use_source_texture);
        instance.setParameter("roughness", settings.roughness);
        instance.setParameter("roughnessTexture", roughness_texture.texture, texture_sampler);
        instance.setParameter("useRoughnessTexture", roughness_texture.use_source_texture);
        instance.setParameter("metallic", settings.metallic);
        instance.setParameter("metallicTexture", metallic_texture.texture, texture_sampler);
        instance.setParameter("useMetallicTexture", metallic_texture.use_source_texture);
        instance.setParameter("reflectance", settings.reflectance);
        instance.setParameter("specularFactor", settings.specular_factor);
        instance.setParameter(
            "specularColorFactor",
            filament::RgbType::LINEAR,
            to_filament_color(settings.specular_color_factor));
        instance.setParameter("clearCoat", settings.clear_coat);
        instance.setParameter("clearCoatRoughness", settings.clear_coat_roughness);
        instance.setParameter("emissionColor", filament::RgbType::LINEAR, to_filament_color(settings.emission_color));
        instance.setParameter("emissionTexture", emission_texture.texture, texture_sampler);
        instance.setParameter("useEmissionTexture", emission_texture.use_source_texture);
        instance.setParameter("emissionIntensity", settings.emission_intensity);
        instance.setParameter("emissionExposureWeight", settings.emission_exposure_weight);
        instance.setParameter("opacity", settings.opacity);
        instance.setParameter("opacityTexture", opacity_texture.texture, texture_sampler);
        instance.setParameter("useOpacityTexture", opacity_texture.use_source_texture);
        instance.setParameter("transmission", settings.transmission);
        instance.setParameter("ior", settings.ior);
        instance.setParameter("absorption", filament::RgbType::LINEAR, filament_absorption(settings));
        instance.setParameter("thickness", settings.thickness);
        instance.setParameter("microThickness", settings.micro_thickness);
        instance.setParameter("dispersion", settings.dispersion);
        if (pbr_material_variant_uses_mask_threshold(variant)) {
            instance.setMaskThreshold(settings.mask_threshold);
        }
        if (pbr_material_variant_uses_transparency_mode(variant)) {
            instance.setTransparencyMode(filament_transparency_mode(settings.transparency_mode));
        }
        instance.setCullingMode(filament_culling_mode(settings.culling));
        instance.setDoubleSided(settings.double_sided);
        instance.setDepthWrite(settings.depth_write);
        instance.setDepthCulling(settings.depth_culling);
        instance.setColorWrite(settings.color_write);
    }

    void destroy()
    {
        for (filament::Material*& material : materials) {
            if (material != nullptr) {
                engine.destroy(material);
                material = nullptr;
            }
        }

        destroy_textures();
    }

private:
    filament::Texture* create_texture(
        uint32_t width,
        uint32_t height,
        filament::Texture::InternalFormat internal_format,
        filament::backend::PixelDataFormat pixel_format,
        std::span<const uint8_t> pixels)
    {
        filament::Texture* texture = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(internal_format)
            .build(engine);
        texture = require_filament_allocation(texture, "material texture");

        texture->setImage(engine, 0, make_filament_pixel_buffer_copy(pixels, pixel_format));
        return texture;
    }

    filament::Texture* load_texture(
        std::string_view source_path,
        PreviewTextureEncoding encoding)
    {
        const ImageColorEncoding image_encoding =
            encoding == PreviewTextureEncoding::srgb_rgba
                ? ImageColorEncoding::srgb
                : ImageColorEncoding::linear;
        const Rgba8Image image = load_rgba8_image_from_file(source_path, image_encoding);
        if (!image.has_pixels()) {
            throw std::runtime_error("Image decoder returned no pixels for " + std::string(source_path));
        }

        if (encoding == PreviewTextureEncoding::srgb_rgba) {
            return create_texture(
                image.width,
                image.height,
                filament::Texture::InternalFormat::SRGB8_A8,
                filament::backend::PixelDataFormat::RGBA,
                std::span<const uint8_t>(image.rgba.data(), image.rgba.size()));
        }

        const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        std::vector<uint8_t> red_channel(pixel_count);
        for (size_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
            red_channel[pixel_index] = image.rgba[pixel_index * 4];
        }

        return create_texture(
            image.width,
            image.height,
            filament::Texture::InternalFormat::R8,
            filament::backend::PixelDataFormat::R,
            std::span<const uint8_t>(red_channel.data(), red_channel.size()));
    }

    void ensure_fallback_textures()
    {
        if (fallback_albedo_texture == nullptr) {
            constexpr std::array<uint8_t, 4> white_srgb_rgba { 255, 255, 255, 255 };
            fallback_albedo_texture = create_texture(
                1,
                1,
                filament::Texture::InternalFormat::SRGB8_A8,
                filament::backend::PixelDataFormat::RGBA,
                std::span<const uint8_t>(white_srgb_rgba.data(), white_srgb_rgba.size()));
        }

        if (fallback_scalar_texture == nullptr) {
            constexpr std::array<uint8_t, 1> white_linear_scalar { 255 };
            fallback_scalar_texture = create_texture(
                1,
                1,
                filament::Texture::InternalFormat::R8,
                filament::backend::PixelDataFormat::R,
                std::span<const uint8_t>(white_linear_scalar.data(), white_linear_scalar.size()));
        }
    }

    MaterialTextureBinding resolve_texture(
        const TextureAssetReference& reference,
        PbrMaterialTextureSlot slot,
        PreviewTextureEncoding encoding,
        filament::Texture* fallback_texture)
    {
        if (!reference.has_source()) {
            return MaterialTextureBinding { fallback_texture, false };
        }

        const std::string cache_key = material_texture_cache_key(reference.source_path, slot, encoding);
        auto [entry, inserted] = texture_cache.try_emplace(cache_key);
        if (inserted) {
            try {
                entry->second.texture = load_texture(reference.source_path, encoding);
            } catch (const std::exception& error) {
                QUADER_LOG_WARN(
                    "Failed to load {} material texture '{}': {}",
                    texture_slot_name(slot),
                    reference.source_path,
                    error.what());
            }
        }

        if (entry->second.texture == nullptr) {
            return MaterialTextureBinding { fallback_texture, false };
        }

        return MaterialTextureBinding { entry->second.texture, true };
    }

    void destroy_textures()
    {
        for (auto& [_, cached] : texture_cache) {
            if (cached.texture != nullptr) {
                engine.destroy(cached.texture);
                cached.texture = nullptr;
            }
        }
        texture_cache.clear();

        if (fallback_scalar_texture != nullptr) {
            engine.destroy(fallback_scalar_texture);
            fallback_scalar_texture = nullptr;
        }

        if (fallback_albedo_texture != nullptr) {
            engine.destroy(fallback_albedo_texture);
            fallback_albedo_texture = nullptr;
        }
    }

    filament::Engine& engine;
    std::array<filament::Material*, static_cast<size_t>(FilamentPbrMaterialVariant::count)> materials {};
    filament::Texture* fallback_albedo_texture = nullptr;
    filament::Texture* fallback_scalar_texture = nullptr;
    std::unordered_map<std::string, CachedMaterialTexture> texture_cache;
    filament::TextureSampler texture_sampler {
        filament::TextureSampler::MinFilter::LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT,
    };
};

FilamentPbrMaterialResources::FilamentPbrMaterialResources(filament::Engine& engine)
    : resources_(std::make_unique<Resources>(engine))
{
}

FilamentPbrMaterialResources::~FilamentPbrMaterialResources() = default;

filament::Material* FilamentPbrMaterialResources::ensure_material(FilamentPbrMaterialVariant variant)
{
    return resources_->ensure_material(variant);
}

filament::MaterialInstance* FilamentPbrMaterialResources::create_mesh_material_instance(
    ViewportMeshKind mesh_kind,
    FilamentPbrMaterialVariant variant)
{
    return resources_->create_mesh_material_instance(mesh_kind, variant);
}

void FilamentPbrMaterialResources::apply_settings(
    filament::MaterialInstance& instance,
    FilamentPbrMaterialVariant variant,
    PbrMaterialSettings settings)
{
    resources_->apply_settings(instance, variant, settings);
}

void FilamentPbrMaterialResources::destroy()
{
    resources_->destroy();
}

} // namespace quader
