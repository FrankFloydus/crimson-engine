#include <gtest/gtest.h>

#include <limits>
#include <vector>

import quader.render.types;
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
}

TEST(RenderTypes, MaterialTextureReferenceSelectsRequestedSlot)
{
    quader::PbrMaterialSettings settings;

    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::albedo).source_path = "albedo.png";
    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::roughness).source_path = "roughness.png";
    quader::material_texture_reference(settings, quader::PbrMaterialTextureSlot::metallic).source_path = "metallic.png";

    EXPECT_EQ("albedo.png", settings.albedo_texture.source_path);
    EXPECT_EQ("roughness.png", settings.roughness_texture.source_path);
    EXPECT_EQ("metallic.png", settings.metallic_texture.source_path);

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
    settings.yaw_degrees = 540.0f;
    settings.pitch_degrees = -120.0f;

    const quader::DirectionalLightSettings clamped = quader::clamp_directional_light_settings(settings);

    EXPECT_FLOAT_EQ(1.0f, clamped.color.r);
    EXPECT_FLOAT_EQ(0.0f, clamped.color.g);
    EXPECT_FLOAT_EQ(0.25f, clamped.color.b);
    EXPECT_FLOAT_EQ(200000.0f, clamped.intensity);
    EXPECT_FLOAT_EQ(180.0f, clamped.yaw_degrees);
    EXPECT_FLOAT_EQ(-89.0f, clamped.pitch_degrees);
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

TEST(ViewportTypes, TextureHandlePayloadIsNotPresentableUntilGpuPresentationExists)
{
    quader::ViewportFramePayload payload;
    payload.texture = quader::RenderTextureHandle {
        .value = 42,
        .extent = { 320, 180 },
    };

    EXPECT_FALSE(payload.has_pixels());
    EXPECT_FALSE(payload.has_presentable_pixels());
    EXPECT_TRUE(payload.has_unsupported_texture_payload());
}

TEST(ViewportTypes, TemporaryReadbackPayloadIsPresentable)
{
    quader::ViewportFramePayload payload;
    payload.temporary_readback = quader::TemporaryRgba8ReadbackFrame {
        .extent = { 2, 2 },
        .rgba = std::vector<uint8_t>(16, 255),
    };

    EXPECT_TRUE(payload.has_pixels());
    EXPECT_TRUE(payload.has_presentable_pixels());
    EXPECT_FALSE(payload.has_unsupported_texture_payload());
}
