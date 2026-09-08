////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "scene/light_properties.hpp"
#include "scene/scene_document.hpp"

#include <filament/Color.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_set>

namespace quader {
namespace {

/// Compares all authored light fields through the native value equality contract.
void expect_api_light(const QuaderLightComponent &actual,
                      const LightComponent &expected) {
  EXPECT_EQ(to_scene_light_component(actual), expected);
}

TEST(LightPropertiesTests, NewDefaultsAndApiConversionsPreserveAllMetadata) {
  const LightComponent defaults;
  EXPECT_FLOAT_EQ(defaults.intensity, 16.0f);
  EXPECT_FLOAT_EQ(defaults.distance_attenuation, 1.0f);
  EXPECT_EQ(defaults.light_unit, LightUnit::Simple);
  EXPECT_EQ(defaults.color_mode, LightColorMode::Color);
  EXPECT_EQ(defaults.source_preset, LightSourcePreset::Custom);
  EXPECT_FLOAT_EQ(defaults.color_temperature_kelvin, 6500.0f);
  EXPECT_FLOAT_EQ(default_quader_point_light_component().distance_attenuation,
                  1.0f);
  EXPECT_FLOAT_EQ(default_quader_point_light_component().intensity, 16.0f);
  EXPECT_FLOAT_EQ(default_quader_directional_light_component().intensity, 16.0f);
  EXPECT_FLOAT_EQ(default_quader_directional_light_component().distance_attenuation,
                  1.0f);
  for (const auto &preset : light_source_presets()) {
    for (const auto unit : {LightUnit::Simple, LightUnit::Lumens,
                            LightUnit::Candela}) {
      LightComponent light;
      ASSERT_TRUE(apply_light_source_preset(light, preset.preset));
      light.light_unit = unit;
      light.type = LightType::Spot;
      light.range = 73.0f;
      light.distance_attenuation = 3.25f;
      light.casts_shadows = true;
      light.intensity = 500.125f;
      expect_api_light(to_api_light_component(light), light);
    }
  }
}

TEST(LightPropertiesTests, EngineOutputsShareOneScaleAcrossShapesAndLegacyUnits) {
  struct IntensityReference {
    float intensity;
    float preview_energy;
    double physical_output;
  };
  constexpr std::array kReferences = {
      IntensityReference{1.0f, 0.0625f, 7539.822368615504},
      IntensityReference{16.0f, 1.0f, 120637.15789784807},
      IntensityReference{32.0f, 2.0f, 241274.31579569614},
  };
  for (const auto shape : {LightType::Directional, LightType::Point,
                           LightType::Spot}) {
    for (const auto unit : {LightUnit::Simple, LightUnit::Lumens,
                            LightUnit::Candela}) {
      for (const auto &reference : kReferences) {
        SCOPED_TRACE(reference.intensity);
        SCOPED_TRACE(static_cast<int>(shape));
        SCOPED_TRACE(static_cast<int>(unit));
        LightComponent light;
        light.type = shape;
        light.light_unit = unit;
        light.intensity = reference.intensity;
        const auto before = light;
        const auto output = light_engine_values(light);
        EXPECT_FLOAT_EQ(output.preview_energy, reference.preview_energy);
        const float physical = shape == LightType::Directional
            ? output.illuminance_lux : output.luminous_intensity_candela;
        EXPECT_NEAR(physical, reference.physical_output, 0.04);
        EXPECT_NEAR(physical * kLightPreviewExposure / std::numbers::pi,
                    reference.preview_energy, 0.000001);
        EXPECT_FLOAT_EQ(shape == LightType::Directional
                            ? output.luminous_intensity_candela
                            : output.illuminance_lux,
                        0.0f);
        EXPECT_EQ(light, before);
      }
    }
  }
}

TEST(LightPropertiesTests, EngineOutputRetainsExtendedAuthoredIntensity) {
  LightComponent light;
  light.type = LightType::Point;
  light.intensity = 0.25f;
  EXPECT_FLOAT_EQ(light_engine_values(light).preview_energy, 0.015625f);
  light.intensity = 512.0f;
  EXPECT_FLOAT_EQ(light_engine_values(light).preview_energy, 32.0f);
  EXPECT_FLOAT_EQ(light.intensity, 512.0f);
  light.intensity = 0.0f;
  EXPECT_FLOAT_EQ(light_engine_values(light).luminous_intensity_candela, 0.0f);
  light.intensity = std::numeric_limits<float>::max();
  EXPECT_TRUE(std::isfinite(light_engine_values(light).preview_energy));
  EXPECT_TRUE(std::isfinite(light_engine_values(light).luminous_intensity_candela));
  EXPECT_FLOAT_EQ(light.intensity, std::numeric_limits<float>::max());
}

TEST(LightPropertiesTests, TemperatureMatchesFilamentReferenceAcrossSliderRange) {
  for (const float kelvin : {1500.0f, 1900.0f, 3200.0f, 6500.0f, 9000.0f,
                             15000.0f}) {
    SCOPED_TRACE(kelvin);
    const QVec3 actual = light_color_temperature_rgb(kelvin);
    const auto reference = filament::Color::cct(kelvin);
    EXPECT_NEAR(actual.x, reference.r, 0.000005f);
    EXPECT_NEAR(actual.y, reference.g, 0.000005f);
    EXPECT_NEAR(actual.z, reference.b, 0.000005f);
    EXPECT_FLOAT_EQ(std::max({actual.x, actual.y, actual.z}), 1.0f);
  }
  const auto warm = light_color_temperature_rgb(1500.0f);
  const auto cool = light_color_temperature_rgb(15000.0f);
  EXPECT_GT(warm.x, warm.z);
  EXPECT_GT(cool.z, cool.x);
  EXPECT_FLOAT_EQ(light_color_temperature_rgb(0.0f).y, warm.y);
  EXPECT_FLOAT_EQ(light_color_temperature_rgb(20000.0f).x, cool.x);
}

TEST(LightPropertiesTests, PickerBoundaryUsesSrgbWithoutChangingSceneColor) {
  const QVec3 srgb{0.5f, 0.04045f, 1.0f};
  const auto linear = light_srgb_to_linear(srgb);
  EXPECT_NEAR(linear.x, 0.21404114f, 0.0000001f);
  EXPECT_NEAR(linear.y, 0.003130805f, 0.0000001f);
  EXPECT_FLOAT_EQ(linear.z, 1.0f);
  const auto restored = light_linear_to_srgb(linear);
  EXPECT_NEAR(restored.x, srgb.x, 0.0000001f);
  EXPECT_NEAR(restored.y, srgb.y, 0.0000001f);
  EXPECT_NEAR(restored.z, srgb.z, 0.0000001f);
  LightComponent historical;
  historical.color = {0.123f, 0.456f, 0.789f};
  const auto original = historical;
  const auto display = light_linear_to_srgb(historical.color);
  EXPECT_EQ(historical, original);
  EXPECT_GT(display.x, historical.color.x);
}

TEST(LightPropertiesTests, PresetsApplyOnlySourcePropertiesForEveryShape) {
  std::unordered_set<std::string> ids;
  ASSERT_EQ(light_source_presets().size(), 26u);
  for (const auto &preset : light_source_presets()) {
    ASSERT_TRUE(ids.insert(std::string(preset.id)).second);
    ASSERT_FALSE(preset.fallback_label.empty());
    float first_shape_intensity = 0.0f;
    for (const auto shape : {LightType::Directional, LightType::Point,
                             LightType::Spot}) {
      LightComponent light;
      light.type = shape;
      light.intensity = 17.25f;
      light.light_unit = LightUnit::Candela;
      light.range = 46.0f;
      light.distance_attenuation = 1.25f;
      light.casts_shadows = true;
      ASSERT_TRUE(apply_light_source_preset(light, preset.preset));
      EXPECT_EQ(light.type, shape);
      EXPECT_EQ(light.light_unit, LightUnit::Candela);
      EXPECT_FLOAT_EQ(light.range, 46.0f);
      EXPECT_FLOAT_EQ(light.distance_attenuation, 1.25f);
      EXPECT_TRUE(light.casts_shadows);
      EXPECT_EQ(light.source_preset, preset.preset);
      EXPECT_TRUE(light_component_metadata_is_valid(light));
      if (preset.preset != LightSourcePreset::Custom) {
        EXPECT_EQ(light.color_mode, LightColorMode::ColorTemperature);
        EXPECT_FLOAT_EQ(light.color_temperature_kelvin, preset.temperature_kelvin);
        EXPECT_GE(light.intensity, 1.0f);
        EXPECT_LE(light.intensity, 32.0f);
        const auto expected_color =
            light_color_temperature_rgb(preset.temperature_kelvin);
        EXPECT_FLOAT_EQ(light.color.x, expected_color.x);
        EXPECT_FLOAT_EQ(light.color.y, expected_color.y);
        EXPECT_FLOAT_EQ(light.color.z, expected_color.z);
        if (shape == LightType::Directional) {
          first_shape_intensity = light.intensity;
        } else {
          EXPECT_FLOAT_EQ(light.intensity, first_shape_intensity);
        }
      } else {
        EXPECT_FLOAT_EQ(light.intensity, 17.25f);
        EXPECT_EQ(light.color_mode, LightColorMode::Color);
      }
    }
  }
  LightComponent candle;
  candle.type = LightType::Point;
  ASSERT_TRUE(apply_light_source_preset(candle, LightSourcePreset::Candle));
  EXPECT_FLOAT_EQ(candle.color_temperature_kelvin, 1900.0f);
  EXPECT_FLOAT_EQ(candle.intensity, 1.0f);
  LightComponent halogen;
  halogen.type = LightType::Point;
  ASSERT_TRUE(apply_light_source_preset(halogen, LightSourcePreset::Halogen80W));
  EXPECT_FLOAT_EQ(halogen.color_temperature_kelvin, 2900.0f);
  EXPECT_GT(halogen.intensity, 16.0f);
  EXPECT_LT(halogen.intensity, 20.0f);
  auto custom_expected = halogen;
  custom_expected.source_preset = LightSourcePreset::Custom;
  ASSERT_TRUE(apply_light_source_preset(halogen, LightSourcePreset::Custom));
  EXPECT_EQ(halogen, custom_expected);
  EXPECT_FALSE(apply_light_source_preset(halogen, static_cast<LightSourcePreset>(999)));
  EXPECT_EQ(halogen, custom_expected);
}

TEST(LightPropertiesTests, PresetsMapNominalBrightnessToBoundedArtistDefaults) {
  const auto intensity = [](LightSourcePreset preset) {
    LightComponent light;
    EXPECT_TRUE(apply_light_source_preset(light, preset));
    return light.intensity;
  };
  // An 800-lumen lamp and the daylight reference are the two medium anchors.
  for (const auto preset : {LightSourcePreset::Incandescent60W,
                            LightSourcePreset::WarmLed,
                            LightSourcePreset::WarmWhiteLed,
                            LightSourcePreset::DaylightLed,
                            LightSourcePreset::CoolDaylightLed,
                            LightSourcePreset::Daylight}) {
    EXPECT_FLOAT_EQ(intensity(preset), 16.0f);
  }
  EXPECT_FLOAT_EQ(intensity(LightSourcePreset::Candle), 1.0f);
  EXPECT_FLOAT_EQ(intensity(LightSourcePreset::SunriseSunset), 1.0f);
  EXPECT_FLOAT_EQ(intensity(LightSourcePreset::Incandescent100W), 20.0f);
  EXPECT_FLOAT_EQ(intensity(LightSourcePreset::MetalHalide400W), 32.0f);
  EXPECT_FLOAT_EQ(intensity(LightSourcePreset::StudioTungsten), 32.0f);
  EXPECT_LT(intensity(LightSourcePreset::Incandescent40W),
            intensity(LightSourcePreset::Incandescent60W));
  EXPECT_GT(intensity(LightSourcePreset::Incandescent75W),
            intensity(LightSourcePreset::Incandescent60W));
  EXPECT_LT(intensity(LightSourcePreset::Incandescent75W),
            intensity(LightSourcePreset::Incandescent100W));
  EXPECT_LT(intensity(LightSourcePreset::Overcast),
            intensity(LightSourcePreset::OutdoorShade));
  EXPECT_LT(intensity(LightSourcePreset::OutdoorShade),
            intensity(LightSourcePreset::BlueSky));
  EXPECT_LT(intensity(LightSourcePreset::BlueSky),
            intensity(LightSourcePreset::MiddaySun));
  EXPECT_LT(intensity(LightSourcePreset::MiddaySun),
            intensity(LightSourcePreset::Daylight));
}

TEST(LightPropertiesTests, SavedIntensityRetainsItsNumberUntilPresetIsReapplied) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  LightComponent historical;
  historical.type = LightType::Directional;
  historical.intensity = 0.25f;
  historical.source_preset = LightSourcePreset::Daylight;
  historical.color_mode = LightColorMode::ColorTemperature;
  historical.color_temperature_kelvin = 6000.0f;
  historical.color = light_color_temperature_rgb(6000.0f);
  QuaderCreateDirectionalLightArgs args;
  args.light = to_api_light_component(historical);
  const auto created = editor.scene().create_directional_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto path = std::filesystem::temp_directory_path() /
                    "quader-light-preset-authored-intensity.qdr";
  const auto saved = editor.scene_documents().save_as_native_path(
      *editor.scene_documents().active_scene_id(), path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  auto reopened = create_standalone_editor();
  auto &restored_editor = reopened.editor();
  const auto opened = restored_editor.scene_documents().open_native_path(path);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  const auto lights = restored_editor.query().entities();
  const auto found = std::find_if(lights.begin(), lights.end(),
                                 [](const auto &entity) { return entity.has_light; });
  ASSERT_NE(found, lights.end());
  expect_api_light(found->light, historical);
  EXPECT_FALSE(restored_editor.project().dirty());
  auto reapplied = to_scene_light_component(found->light);
  ASSERT_TRUE(apply_light_source_preset(reapplied, LightSourcePreset::Daylight));
  EXPECT_FLOAT_EQ(reapplied.intensity, 16.0f);
  const auto changed = restored_editor.scene().set_light_component(
      found->id, to_api_light_component(reapplied));
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  ASSERT_TRUE(changed.changed());
  ASSERT_TRUE(restored_editor.history().undo().ok());
  expect_api_light(restored_editor.query().entity(found->id)->light, historical);
  // Accepted edits use monotonic revisions, including ordinary Undo.
  EXPECT_TRUE(restored_editor.project().dirty());
  ASSERT_TRUE(restored_editor.history().redo().ok());
  expect_api_light(restored_editor.query().entity(found->id)->light, reapplied);
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(LightPropertiesTests, SceneRejectsInvalidMetadataWithoutPublishingOrClamping) {
  SceneDocument scene;
  const auto entity = scene.create_entity("Light", EntityKind::Light);
  LightComponent original;
  original.intensity = 999.0f;
  original.distance_attenuation = 3.5f;
  ASSERT_TRUE(scene.set_light(entity, original));
  const auto revision = scene.revision();
  const auto check_rejected = [&](LightComponent invalid) {
    EXPECT_FALSE(scene.set_light(entity, invalid));
    EXPECT_EQ(scene.revision(), revision);
    ASSERT_NE(scene.light(entity), nullptr);
    EXPECT_EQ(*scene.light(entity), original);
  };
  auto invalid = original;
  invalid.light_unit = static_cast<LightUnit>(3);
  check_rejected(invalid);
  invalid = original;
  invalid.color_mode = static_cast<LightColorMode>(2);
  check_rejected(invalid);
  invalid = original;
  invalid.source_preset = static_cast<LightSourcePreset>(26);
  check_rejected(invalid);
  for (const float kelvin : {1499.0f, 15001.0f,
                             std::numeric_limits<float>::quiet_NaN()}) {
    invalid = original;
    invalid.color_temperature_kelvin = kelvin;
    check_rejected(invalid);
  }
}

TEST(LightPropertiesTests, PublicMetadataEditsDuplicateUndoRedoAndPersistExactly) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  LightComponent initial;
  initial.type = LightType::Point;
  initial.light_unit = LightUnit::Lumens;
  ASSERT_TRUE(apply_light_source_preset(initial, LightSourcePreset::Halogen80W));
  QuaderCreatePointLightArgs args;
  args.name = "Source Metadata Light";
  args.light = to_api_light_component(initial);
  const auto created = editor.scene().create_point_light(args);
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto entity = created.value().id;
  ASSERT_TRUE(editor.query().entity(entity).has_value());
  expect_api_light(editor.query().entity(entity)->light, initial);

  auto edited = initial;
  edited.light_unit = LightUnit::Candela;
  edited.intensity = 512.125f;
  edited.color_temperature_kelvin = 15000.0f;
  edited.color = light_color_temperature_rgb(15000.0f);
  edited.source_preset = LightSourcePreset::Custom;
  auto changed = editor.scene().set_light_component(entity, to_api_light_component(edited));
  ASSERT_TRUE(changed.ok()) << changed.error_message();
  ASSERT_TRUE(changed.changed());
  expect_api_light(editor.query().entity(entity)->light, edited);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_api_light(editor.query().entity(entity)->light, initial);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_api_light(editor.query().entity(entity)->light, edited);

  const auto duplicated = editor.scene().duplicate(entity);
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  const auto duplicate_id = duplicated.value().id;
  ASSERT_TRUE(editor.query().entity(duplicate_id).has_value());
  expect_api_light(editor.query().entity(duplicate_id)->light, edited);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.query().entity(duplicate_id).has_value());
  ASSERT_TRUE(editor.history().redo().ok());
  ASSERT_TRUE(editor.query().entity(duplicate_id).has_value());
  expect_api_light(editor.query().entity(duplicate_id)->light, edited);

  const auto path = std::filesystem::temp_directory_path() /
                    "quader-light-properties-metadata-roundtrip.qdr";
  const auto saved = editor.scene_documents().save_as_native_path(
      *editor.scene_documents().active_scene_id(), path);
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  EXPECT_FALSE(editor.project().dirty());
  auto reopened = create_standalone_editor();
  const auto opened = reopened.editor().scene_documents().open_native_path(path);
  ASSERT_TRUE(opened.ok()) << opened.error_message();
  std::size_t restored_count = 0u;
  for (const auto &restored : reopened.editor().query().entities()) {
    if (restored.has_light) {
      ++restored_count;
      expect_api_light(restored.light, edited);
    }
  }
  EXPECT_EQ(restored_count, 2u);
  EXPECT_FALSE(reopened.editor().project().dirty());
  std::error_code ignored;
  std::filesystem::remove(path, ignored);
}

TEST(LightPropertiesTests, PublicMetadataValidationPreservesHistoryAndScene) {
  auto standalone = create_standalone_editor();
  auto &editor = standalone.editor();
  const auto created = editor.scene().create_point_light();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto original = editor.query().entity(created.value().id)->light;
  auto invalid = original;
  invalid.color_temperature_kelvin = 1499.0f;
  const auto revision = editor.revisions().scene_revision;
  const auto failed = editor.scene().set_light_component(created.value().id, invalid);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(editor.revisions().scene_revision, revision);
  expect_api_light(editor.query().entity(created.value().id)->light,
                    to_scene_light_component(original));
  QuaderCreatePointLightArgs args;
  args.light.light_unit = static_cast<QuaderLightUnit>(999);
  EXPECT_FALSE(editor.scene().create_point_light(args).ok());
  EXPECT_EQ(editor.revisions().scene_revision, revision);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_FALSE(editor.query().entity(created.value().id).has_value());
}

} // namespace
} // namespace quader
