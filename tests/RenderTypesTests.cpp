#include <gtest/gtest.h>

#include <limits>
#include <vector>

import quader.render.types;
import quader.render.viewport_projection_policy;
import quader.render.viewport_types;

TEST(RenderTypes, DefaultPbrMaterialIsMatteWhiteDielectric)
{
    const quader::PbrMaterialSettings settings;

    EXPECT_FLOAT_EQ(1.0f, settings.albedo.r);
    EXPECT_FLOAT_EQ(1.0f, settings.albedo.g);
    EXPECT_FLOAT_EQ(1.0f, settings.albedo.b);
    EXPECT_FALSE(settings.albedo_texture.has_source());
    EXPECT_FLOAT_EQ(1.0f, settings.roughness);
    EXPECT_FALSE(settings.roughness_texture.has_source());
    EXPECT_FLOAT_EQ(0.0f, settings.metallic);
    EXPECT_FALSE(settings.metallic_texture.has_source());
    EXPECT_FLOAT_EQ(0.8f, settings.reflectance);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_factor);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_color_factor.r);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_color_factor.g);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_color_factor.b);
    EXPECT_FLOAT_EQ(0.0f, settings.clear_coat);
    EXPECT_FLOAT_EQ(0.0f, settings.clear_coat_roughness);
    EXPECT_EQ(quader::PbrMaterialBlendMode::opaque, settings.blend_mode);
    EXPECT_EQ(quader::PbrMaterialTransparencyMode::default_sorting, settings.transparency_mode);
    EXPECT_EQ(quader::PbrMaterialCullingMode::back, settings.culling);
    EXPECT_TRUE(settings.double_sided);
    EXPECT_TRUE(settings.depth_write);
    EXPECT_TRUE(settings.depth_culling);
    EXPECT_TRUE(settings.color_write);
    EXPECT_TRUE(settings.alpha_to_coverage);
    EXPECT_FALSE(settings.transparent_shadow);
    EXPECT_FLOAT_EQ(1.0f, settings.opacity);
    EXPECT_FALSE(settings.opacity_texture.has_source());
    EXPECT_FLOAT_EQ(0.4f, settings.mask_threshold);
    EXPECT_EQ(quader::PbrMaterialRefractionMode::none, settings.refraction_mode);
    EXPECT_EQ(quader::PbrMaterialRefractionType::solid, settings.refraction_type);
    EXPECT_FLOAT_EQ(0.0f, settings.transmission);
    EXPECT_FLOAT_EQ(1.5f, settings.ior);
    EXPECT_EQ(quader::PbrMaterialAbsorptionMode::coefficients, settings.absorption_mode);
    EXPECT_FLOAT_EQ(0.0f, settings.absorption.x);
    EXPECT_FLOAT_EQ(0.0f, settings.absorption.y);
    EXPECT_FLOAT_EQ(0.0f, settings.absorption.z);
    EXPECT_FLOAT_EQ(1.0f, settings.transmittance_color.r);
    EXPECT_FLOAT_EQ(1.0f, settings.transmittance_color.g);
    EXPECT_FLOAT_EQ(1.0f, settings.transmittance_color.b);
    EXPECT_FLOAT_EQ(1.0f, settings.transmittance_distance);
    EXPECT_FLOAT_EQ(0.5f, settings.thickness);
    EXPECT_FLOAT_EQ(0.0f, settings.micro_thickness);
    EXPECT_FLOAT_EQ(0.0f, settings.dispersion);
}

TEST(RenderTypes, MaterialTextureReferenceSelectsRequestedSlot)
{
    quader::PbrMaterialSettings settings;

    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::albedo).source_path = "albedo.png";
    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::roughness).source_path = "roughness.png";
    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::metallic).source_path = "metallic.png";
    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::opacity).source_path = "opacity.png";

    EXPECT_EQ("albedo.png", settings.albedo_texture.source_path);
    EXPECT_EQ("roughness.png", settings.roughness_texture.source_path);
    EXPECT_EQ("metallic.png", settings.metallic_texture.source_path);
    EXPECT_EQ("opacity.png", settings.opacity_texture.source_path);

    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::roughness).clear();
    EXPECT_FALSE(settings.roughness_texture.has_source());
}

TEST(RenderTypes, ClampPbrMaterialSettings)
{
    quader::PbrMaterialSettings settings;
    settings.albedo = { -1.0f, 0.5f, 2.0f };
    settings.roughness = -0.25f;
    settings.metallic = 1.25f;
    settings.reflectance = -0.5f;
    settings.specular_factor = 2.0f;
    settings.specular_color_factor = { 1.5f, -0.25f, 0.75f };
    settings.clear_coat = 1.5f;
    settings.clear_coat_roughness = -1.0f;
    settings.blend_mode = static_cast<quader::PbrMaterialBlendMode>(255);
    settings.transparency_mode = static_cast<quader::PbrMaterialTransparencyMode>(255);
    settings.culling = static_cast<quader::PbrMaterialCullingMode>(255);
    settings.opacity = std::numeric_limits<float>::quiet_NaN();
    settings.mask_threshold = 1.5f;
    settings.refraction_mode = static_cast<quader::PbrMaterialRefractionMode>(255);
    settings.refraction_type = static_cast<quader::PbrMaterialRefractionType>(255);
    settings.transmission = -0.5f;
    settings.ior = 9.0f;
    settings.absorption_mode = static_cast<quader::PbrMaterialAbsorptionMode>(255);
    settings.absorption = { -1.0f, std::numeric_limits<float>::infinity(), 2.5f };
    settings.transmittance_color = { -0.25f, 0.5f, 1.5f };
    settings.transmittance_distance = -2.0f;
    settings.thickness = std::numeric_limits<float>::quiet_NaN();
    settings.micro_thickness = -0.1f;
    settings.dispersion = 1.5f;

    const quader::PbrMaterialSettings clamped = quader::clamp_pbr_material_settings(settings);

    EXPECT_FLOAT_EQ(0.0f, clamped.albedo.r);
    EXPECT_FLOAT_EQ(0.5f, clamped.albedo.g);
    EXPECT_FLOAT_EQ(1.0f, clamped.albedo.b);
    EXPECT_FLOAT_EQ(0.0f, clamped.roughness);
    EXPECT_FLOAT_EQ(1.0f, clamped.metallic);
    EXPECT_FLOAT_EQ(0.0f, clamped.reflectance);
    EXPECT_FLOAT_EQ(1.0f, clamped.specular_factor);
    EXPECT_FLOAT_EQ(1.0f, clamped.specular_color_factor.r);
    EXPECT_FLOAT_EQ(0.0f, clamped.specular_color_factor.g);
    EXPECT_FLOAT_EQ(0.75f, clamped.specular_color_factor.b);
    EXPECT_FLOAT_EQ(1.0f, clamped.clear_coat);
    EXPECT_FLOAT_EQ(0.0f, clamped.clear_coat_roughness);
    EXPECT_EQ(quader::PbrMaterialBlendMode::opaque, clamped.blend_mode);
    EXPECT_EQ(quader::PbrMaterialTransparencyMode::default_sorting, clamped.transparency_mode);
    EXPECT_EQ(quader::PbrMaterialCullingMode::back, clamped.culling);
    EXPECT_FLOAT_EQ(1.0f, clamped.opacity);
    EXPECT_FLOAT_EQ(1.0f, clamped.mask_threshold);
    EXPECT_EQ(quader::PbrMaterialRefractionMode::none, clamped.refraction_mode);
    EXPECT_EQ(quader::PbrMaterialRefractionType::solid, clamped.refraction_type);
    EXPECT_FLOAT_EQ(0.0f, clamped.transmission);
    EXPECT_FLOAT_EQ(4.0f, clamped.ior);
    EXPECT_EQ(quader::PbrMaterialAbsorptionMode::coefficients, clamped.absorption_mode);
    EXPECT_FLOAT_EQ(0.0f, clamped.absorption.x);
    EXPECT_FLOAT_EQ(0.0f, clamped.absorption.y);
    EXPECT_FLOAT_EQ(2.5f, clamped.absorption.z);
    EXPECT_FLOAT_EQ(0.0f, clamped.transmittance_color.r);
    EXPECT_FLOAT_EQ(0.5f, clamped.transmittance_color.g);
    EXPECT_FLOAT_EQ(1.0f, clamped.transmittance_color.b);
    EXPECT_FLOAT_EQ(0.001f, clamped.transmittance_distance);
    EXPECT_FLOAT_EQ(0.5f, clamped.thickness);
    EXPECT_FLOAT_EQ(0.0f, clamped.micro_thickness);
    EXPECT_FLOAT_EQ(1.0f, clamped.dispersion);
}

TEST(RenderTypes, DefaultEnvironmentLightingUsesBuiltInIblWithHiddenSkybox)
{
    const quader::EnvironmentLightingSettings settings;

    EXPECT_EQ(quader::EnvironmentLightingSource::built_in_default, settings.source);
    EXPECT_STREQ("Built-in Default", quader::environment_lighting_source_name(settings.source));
    EXPECT_TRUE(settings.enabled);
    EXPECT_FLOAT_EQ(30000.0f, settings.intensity);
    EXPECT_FLOAT_EQ(0.0f, settings.rotation_degrees);
    EXPECT_FALSE(settings.show_skybox);
    EXPECT_FLOAT_EQ(0.055f, settings.fallback_color.r);
    EXPECT_FLOAT_EQ(0.060f, settings.fallback_color.g);
    EXPECT_FLOAT_EQ(0.070f, settings.fallback_color.b);
    EXPECT_FLOAT_EQ(1.0f, settings.fallback_color.a);
}

TEST(RenderTypes, ClampEnvironmentLightingSettings)
{
    quader::EnvironmentLightingSettings settings;
    settings.intensity = std::numeric_limits<float>::infinity();
    settings.rotation_degrees = 725.0f;
    settings.fallback_color = { -1.0f, 0.25f, 2.0f, 5.0f };

    const quader::EnvironmentLightingSettings clamped =
        quader::clamp_environment_lighting_settings(settings);

    EXPECT_FLOAT_EQ(30000.0f, clamped.intensity);
    EXPECT_FLOAT_EQ(5.0f, clamped.rotation_degrees);
    EXPECT_FLOAT_EQ(0.0f, clamped.fallback_color.r);
    EXPECT_FLOAT_EQ(0.25f, clamped.fallback_color.g);
    EXPECT_FLOAT_EQ(1.0f, clamped.fallback_color.b);
    EXPECT_FLOAT_EQ(1.0f, clamped.fallback_color.a);
}

TEST(RenderTypes, ConvertSrgbToLinearWithStandardTransferFunction)
{
    const quader::LinearColor black = quader::srgb_to_linear({ 0.0f, 0.0f, 0.0f });
    const quader::LinearColor white = quader::srgb_to_linear({ 1.0f, 1.0f, 1.0f });
    const quader::LinearColor mid = quader::srgb_to_linear({ 0.5f, 0.5f, 0.5f });

    EXPECT_FLOAT_EQ(0.0f, black.r);
    EXPECT_FLOAT_EQ(1.0f, white.r);
    EXPECT_NEAR(0.214041f, mid.r, 0.000001f);
}

TEST(RenderTypes, ConvertLinearToSrgbWithStandardTransferFunction)
{
    const quader::SrgbColor black = quader::linear_to_srgb({ 0.0f, 0.0f, 0.0f });
    const quader::SrgbColor white = quader::linear_to_srgb({ 1.0f, 1.0f, 1.0f });
    const quader::SrgbColor mid = quader::linear_to_srgb({ 0.214041f, 0.214041f, 0.214041f });

    EXPECT_FLOAT_EQ(0.0f, black.r);
    EXPECT_FLOAT_EQ(1.0f, white.r);
    EXPECT_NEAR(0.5f, mid.r, 0.000001f);
}

TEST(RenderTypes, PremultiplyLinearRgba)
{
    const quader::LinearRgba rgba = quader::premultiplied_linear_rgba({ 0.8f, 0.25f, 1.0f }, 0.5f);

    EXPECT_FLOAT_EQ(0.4f, rgba.r);
    EXPECT_FLOAT_EQ(0.125f, rgba.g);
    EXPECT_FLOAT_EQ(0.5f, rgba.b);
    EXPECT_FLOAT_EQ(0.5f, rgba.a);
}

TEST(RenderTypes, ClampDirectionalLightSettings)
{
    quader::DirectionalLightSettings settings;
    settings.color = { 3.0f, -2.0f, 0.25f };
    settings.intensity = 500000.0f;

    const quader::DirectionalLightSettings clamped = quader::clamp_directional_light_settings(settings);

    EXPECT_FLOAT_EQ(1.0f, clamped.color.r);
    EXPECT_FLOAT_EQ(0.0f, clamped.color.g);
    EXPECT_FLOAT_EQ(0.25f, clamped.color.b);
    EXPECT_FLOAT_EQ(200000.0f, clamped.intensity);
}

TEST(RenderTypes, DefaultDirectionalShadowSettingsAreProductionReady)
{
    const quader::DirectionalLightSettings light;

    EXPECT_TRUE(light.shadow.enabled);
    EXPECT_EQ(quader::DirectionalShadowMode::crisp, light.shadow.mode);
    EXPECT_EQ(quader::DirectionalShadowQuality::high, light.shadow.quality);
    EXPECT_EQ(2048U, quader::directional_shadow_quality_map_size(light.shadow.quality));
    EXPECT_FLOAT_EQ(150.0f, light.shadow.distance);
    EXPECT_EQ(3, light.shadow.cascades);
    EXPECT_FALSE(light.shadow.stable);
    EXPECT_TRUE(light.shadow.contact_shadows);
    EXPECT_FLOAT_EQ(0.3f, light.shadow.contact_distance);
    EXPECT_EQ(8, light.shadow.contact_steps);
    EXPECT_FLOAT_EQ(0.001f, light.shadow.constant_bias);
    EXPECT_FLOAT_EQ(1.0f, light.shadow.normal_bias);
    EXPECT_FLOAT_EQ(0.5f, light.shadow.polygon_offset_constant);
    EXPECT_FLOAT_EQ(2.0f, light.shadow.polygon_offset_slope);
    EXPECT_FLOAT_EQ(1.0f, light.shadow.near_hint);
    EXPECT_FLOAT_EQ(100.0f, light.shadow.far_hint);
    EXPECT_TRUE(light.shadow.lispsm);
    EXPECT_FLOAT_EQ(0.0f, light.shadow.vsm_blur_width);
    EXPECT_FALSE(light.shadow.vsm_elvsm);
    EXPECT_FALSE(light.shadow.vsm_high_precision);
    EXPECT_FLOAT_EQ(0.15f, light.shadow.vsm_light_bleed_reduction);
    EXPECT_FLOAT_EQ(0.02f, light.shadow.soft_shadow_bulb_radius);
    EXPECT_FLOAT_EQ(1.0f, light.shadow.penumbra_scale);
    EXPECT_FLOAT_EQ(1.0f, light.shadow.penumbra_ratio_scale);
}

TEST(RenderTypes, ClampPointLightSettings)
{
    quader::PointLightSettings settings;
    settings.color = { 2.0f, -1.0f, 0.25f };
    settings.intensity = std::numeric_limits<float>::infinity();
    settings.range = -5.0f;
    settings.shadow.constant_bias = -1.0f;
    settings.shadow.normal_bias = -2.0f;

    const quader::PointLightSettings clamped = quader::clamp_point_light_settings(settings);

    EXPECT_FLOAT_EQ(1.0f, clamped.color.r);
    EXPECT_FLOAT_EQ(0.0f, clamped.color.g);
    EXPECT_FLOAT_EQ(0.25f, clamped.color.b);
    EXPECT_FLOAT_EQ(1000000.0f, clamped.intensity);
    EXPECT_FLOAT_EQ(0.0f, clamped.range);
    EXPECT_EQ(quader::PointLightFalloffMode::linear, clamped.falloff_mode);
    EXPECT_FLOAT_EQ(0.0f, clamped.shadow.constant_bias);
    EXPECT_FLOAT_EQ(0.0f, clamped.shadow.normal_bias);
}

TEST(RenderTypes, PointLightIntensitySupportsBrightEditorValues)
{
    const quader::PointLightSettings defaults;
    EXPECT_FLOAT_EQ(1000000.0f, defaults.intensity);
    EXPECT_EQ(quader::PointLightFalloffMode::linear, defaults.falloff_mode);
    EXPECT_STREQ("Linear", quader::point_light_falloff_mode_name(defaults.falloff_mode));
    EXPECT_STREQ(
        "Inverse Squared",
        quader::point_light_falloff_mode_name(quader::PointLightFalloffMode::inverse_squared));

    quader::PointLightSettings settings;
    settings.intensity = 500000000.0f;

    const quader::PointLightSettings clamped = quader::clamp_point_light_settings(settings);
    EXPECT_FLOAT_EQ(100000000.0f, clamped.intensity);
}

TEST(RenderTypes, DirectionalShadowQualityMapsToTextureSize)
{
    EXPECT_EQ(512U, quader::directional_shadow_quality_map_size(quader::DirectionalShadowQuality::low));
    EXPECT_EQ(1024U, quader::directional_shadow_quality_map_size(quader::DirectionalShadowQuality::medium));
    EXPECT_EQ(2048U, quader::directional_shadow_quality_map_size(quader::DirectionalShadowQuality::high));
    EXPECT_EQ(4096U, quader::directional_shadow_quality_map_size(quader::DirectionalShadowQuality::ultra));
}

TEST(RenderTypes, ClampDirectionalShadowSettings)
{
    quader::DirectionalShadowSettings settings;
    settings.distance = std::numeric_limits<float>::quiet_NaN();
    settings.cascades = 99;
    settings.contact_distance = -1.0f;
    settings.contact_steps = 0;
    settings.constant_bias = -0.25f;
    settings.normal_bias = std::numeric_limits<float>::infinity();
    settings.polygon_offset_constant = -2.0f;
    settings.polygon_offset_slope = std::numeric_limits<float>::quiet_NaN();
    settings.near_hint = -5.0f;
    settings.far_hint = std::numeric_limits<float>::infinity();
    settings.vsm_blur_width = 500.0f;
    settings.vsm_light_bleed_reduction = -0.5f;
    settings.soft_shadow_bulb_radius = -3.0f;
    settings.penumbra_scale = std::numeric_limits<float>::quiet_NaN();
    settings.penumbra_ratio_scale = -4.0f;

    const quader::DirectionalShadowSettings clamped = quader::clamp_directional_shadow_settings(settings);

    EXPECT_FLOAT_EQ(150.0f, clamped.distance);
    EXPECT_EQ(4, clamped.cascades);
    EXPECT_FLOAT_EQ(0.0f, clamped.contact_distance);
    EXPECT_EQ(1, clamped.contact_steps);
    EXPECT_FLOAT_EQ(0.0f, clamped.constant_bias);
    EXPECT_FLOAT_EQ(1.0f, clamped.normal_bias);
    EXPECT_FLOAT_EQ(0.0f, clamped.polygon_offset_constant);
    EXPECT_FLOAT_EQ(2.0f, clamped.polygon_offset_slope);
    EXPECT_FLOAT_EQ(0.0f, clamped.near_hint);
    EXPECT_FLOAT_EQ(100.0f, clamped.far_hint);
    EXPECT_FLOAT_EQ(125.0f, clamped.vsm_blur_width);
    EXPECT_FLOAT_EQ(0.0f, clamped.vsm_light_bleed_reduction);
    EXPECT_FLOAT_EQ(0.0f, clamped.soft_shadow_bulb_radius);
    EXPECT_FLOAT_EQ(1.0f, clamped.penumbra_scale);
    EXPECT_FLOAT_EQ(1.0f, clamped.penumbra_ratio_scale);
}

TEST(ViewportTypes, GpuImagePayloadCarriesRenderOutputButNotCpuPixels)
{
    quader::ViewportFramePayload payload;
    payload.gpu_image = quader::ViewportGpuImageHandle {
        .viewport_id = quader::ViewportId { 3 },
        .generation = quader::ViewportGeneration { 5 },
        .slot_index = 1,
        .image_generation = 7,
        .resource_id = 42,
        .extent = { 320, 180 },
        .readiness = quader::ViewportGpuImageReadiness::ready,
    };

    EXPECT_TRUE(payload.has_ready_gpu_image());
    EXPECT_TRUE(payload.has_render_output());
}

TEST(ViewportTypes, GpuImagePayloadMustBeReadyBeforeItCanBeConsumed)
{
    quader::ViewportFramePayload payload;
    payload.gpu_image = quader::ViewportGpuImageHandle {
        .viewport_id = quader::ViewportId { 3 },
        .generation = quader::ViewportGeneration { 5 },
        .slot_index = 1,
        .image_generation = 7,
        .resource_id = 42,
        .extent = { 320, 180 },
        .readiness = quader::ViewportGpuImageReadiness::rendering,
    };

    EXPECT_FALSE(payload.has_ready_gpu_image());
    EXPECT_TRUE(payload.has_pending_gpu_image());
    EXPECT_FALSE(payload.has_render_output());
}

TEST(ViewportTypes, GpuImagePayloadCanRepresentBackendPendingFrame)
{
    quader::ViewportFramePayload payload;
    payload.gpu_image_pending = true;

    EXPECT_FALSE(payload.has_ready_gpu_image());
    EXPECT_TRUE(payload.has_pending_gpu_image());
    EXPECT_FALSE(payload.has_render_output());
}

TEST(ViewportProjectionPolicy, ClampsOrthographicProjectionToRendererSafeRange)
{
    quader::ViewportProjection projection {
        .kind = quader::ViewportProjectionKind::orthographic,
        .vertical_fov_degrees = 52.0f,
        .orthographic_size = 5.0f,
        .near_plane = 0.01f,
        .far_plane = 1000.0f,
    };

    const quader::ViewportProjection clamped =
        quader::clamp_viewport_projection_for_required_far(projection, 10000.0f);
    const quader::ViewportProjectionLimits limits = quader::viewport_projection_limits();

    EXPECT_EQ(quader::ViewportProjectionKind::orthographic, clamped.kind);
    EXPECT_FLOAT_EQ(limits.minimum_near_plane, clamped.near_plane);
    EXPECT_FLOAT_EQ(10000.0f, clamped.far_plane);
    EXPECT_LE(quader::viewport_projection_far_to_near_ratio(clamped), limits.maximum_far_to_near_ratio);
}

TEST(ViewportProjectionPolicy, RepairsInvalidProjectionInputs)
{
    quader::ViewportProjection projection {
        .kind = quader::ViewportProjectionKind::perspective,
        .vertical_fov_degrees = std::numeric_limits<float>::infinity(),
        .orthographic_size = -1.0f,
        .near_plane = -5.0f,
        .far_plane = std::numeric_limits<float>::quiet_NaN(),
    };

    const quader::ViewportProjection clamped = quader::clamp_viewport_projection(projection);
    const quader::ViewportProjectionLimits limits = quader::viewport_projection_limits();

    EXPECT_EQ(quader::ViewportProjectionKind::perspective, clamped.kind);
    EXPECT_GE(clamped.vertical_fov_degrees, limits.minimum_vertical_fov_degrees);
    EXPECT_LE(clamped.vertical_fov_degrees, limits.maximum_vertical_fov_degrees);
    EXPECT_GE(clamped.orthographic_size, limits.minimum_orthographic_size);
    EXPECT_FLOAT_EQ(limits.minimum_near_plane, clamped.near_plane);
    EXPECT_GT(clamped.far_plane, clamped.near_plane);
    EXPECT_LE(quader::viewport_projection_far_to_near_ratio(clamped), limits.maximum_far_to_near_ratio);
}
