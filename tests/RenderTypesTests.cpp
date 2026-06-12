#include <gtest/gtest.h>

import quader.render.types;

TEST(RenderTypes, DefaultPbrMaterialIsGlossyRedDielectric)
{
    const quader::PbrMaterialSettings settings;

    EXPECT_FLOAT_EQ(1.0f, settings.albedo.r);
    EXPECT_FLOAT_EQ(0.0f, settings.albedo.g);
    EXPECT_FLOAT_EQ(0.0f, settings.albedo.b);
    EXPECT_FLOAT_EQ(0.18f, settings.roughness);
    EXPECT_FLOAT_EQ(0.0f, settings.metallic);
    EXPECT_FLOAT_EQ(0.8f, settings.reflectance);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_factor);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_color_factor.r);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_color_factor.g);
    EXPECT_FLOAT_EQ(1.0f, settings.specular_color_factor.b);
    EXPECT_FLOAT_EQ(0.0f, settings.clear_coat);
    EXPECT_FLOAT_EQ(0.0f, settings.clear_coat_roughness);
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
    settings.pitch_degrees = -120.0f;

    const quader::DirectionalLightSettings clamped = quader::clamp_directional_light_settings(settings);

    EXPECT_FLOAT_EQ(1.0f, clamped.color.r);
    EXPECT_FLOAT_EQ(0.0f, clamped.color.g);
    EXPECT_FLOAT_EQ(0.25f, clamped.color.b);
    EXPECT_FLOAT_EQ(200000.0f, clamped.intensity);
    EXPECT_FLOAT_EQ(-89.0f, clamped.pitch_degrees);
}
