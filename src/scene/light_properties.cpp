////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/light_properties.hpp"

#include "core/color_space.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace quader {
namespace {

constexpr std::array kLightSourcePresets = {
    LightSourcePresetDefinition{LightSourcePreset::Custom, "custom",
      "inspector.light.preset.custom", "Custom", 6500.0f, LightSourcePresetUnit::ColorOnly, 0.0f},
    LightSourcePresetDefinition{LightSourcePreset::Candle, "candle",
      "inspector.light.preset.candle", "Candle", 1900.0f, LightSourcePresetUnit::Lumens, 12.566f},
    LightSourcePresetDefinition{LightSourcePreset::Incandescent40W, "incandescent_40w",
      "inspector.light.preset.incandescent_40w", "Incandescent 40 W", 2700.0f, LightSourcePresetUnit::Lumens, 450.0f},
    LightSourcePresetDefinition{LightSourcePreset::Incandescent60W, "incandescent_60w",
      "inspector.light.preset.incandescent_60w", "Incandescent 60 W", 2700.0f, LightSourcePresetUnit::Lumens, 800.0f},
    LightSourcePresetDefinition{LightSourcePreset::Incandescent75W, "incandescent_75w",
      "inspector.light.preset.incandescent_75w", "Incandescent 75 W", 2700.0f, LightSourcePresetUnit::Lumens, 1100.0f},
    LightSourcePresetDefinition{LightSourcePreset::Incandescent100W, "incandescent_100w",
      "inspector.light.preset.incandescent_100w", "Incandescent 100 W", 2700.0f, LightSourcePresetUnit::Lumens, 1600.0f},
    LightSourcePresetDefinition{LightSourcePreset::Halogen80W, "halogen_80w",
      "inspector.light.preset.halogen_80w", "Halogen 80 W", 2900.0f, LightSourcePresetUnit::Lumens, 1415.0f},
    LightSourcePresetDefinition{LightSourcePreset::DecorativeLed, "decorative_led",
      "inspector.light.preset.decorative_led", "Decorative LED", 1800.0f, LightSourcePresetUnit::Lumens, 470.0f},
    LightSourcePresetDefinition{LightSourcePreset::WarmLed, "warm_led",
      "inspector.light.preset.warm_led", "Warm LED", 2700.0f, LightSourcePresetUnit::Lumens, 800.0f},
    LightSourcePresetDefinition{LightSourcePreset::WarmWhiteLed, "warm_white_led",
      "inspector.light.preset.warm_white_led", "Warm White LED", 3000.0f, LightSourcePresetUnit::Lumens, 800.0f},
    LightSourcePresetDefinition{LightSourcePreset::NeutralLed, "neutral_led",
      "inspector.light.preset.neutral_led", "Neutral LED", 4000.0f, LightSourcePresetUnit::Lumens, 1055.0f},
    LightSourcePresetDefinition{LightSourcePreset::DaylightLed, "daylight_led",
      "inspector.light.preset.daylight_led", "Daylight LED", 5000.0f, LightSourcePresetUnit::Lumens, 800.0f},
    LightSourcePresetDefinition{LightSourcePreset::CoolDaylightLed, "cool_daylight_led",
      "inspector.light.preset.cool_daylight_led", "Cool Daylight LED", 6500.0f, LightSourcePresetUnit::Lumens, 800.0f},
    LightSourcePresetDefinition{LightSourcePreset::WarmFluorescent36W, "warm_fluorescent_36w",
      "inspector.light.preset.warm_fluorescent_36w", "Warm Fluorescent 36 W", 3000.0f, LightSourcePresetUnit::Lumens, 3350.0f},
    LightSourcePresetDefinition{LightSourcePreset::NeutralFluorescent36W, "neutral_fluorescent_36w",
      "inspector.light.preset.neutral_fluorescent_36w", "Neutral Fluorescent 36 W", 4000.0f, LightSourcePresetUnit::Lumens, 3350.0f},
    LightSourcePresetDefinition{LightSourcePreset::DaylightFluorescent36W, "daylight_fluorescent_36w",
      "inspector.light.preset.daylight_fluorescent_36w", "Daylight Fluorescent 36 W", 6500.0f, LightSourcePresetUnit::Lumens, 3200.0f},
    LightSourcePresetDefinition{LightSourcePreset::HighPressureSodium100W, "high_pressure_sodium_100w",
      "inspector.light.preset.high_pressure_sodium_100w", "High-pressure Sodium 100 W", 2000.0f, LightSourcePresetUnit::Lumens, 10600.0f},
    LightSourcePresetDefinition{LightSourcePreset::MercuryVapor80W, "mercury_vapor_80w",
      "inspector.light.preset.mercury_vapor_80w", "Mercury Vapor 80 W", 4200.0f, LightSourcePresetUnit::Lumens, 3600.0f},
    LightSourcePresetDefinition{LightSourcePreset::MetalHalide400W, "metal_halide_400w",
      "inspector.light.preset.metal_halide_400w", "Metal Halide 400 W", 4500.0f, LightSourcePresetUnit::Lumens, 32000.0f},
    LightSourcePresetDefinition{LightSourcePreset::StudioTungsten, "studio_tungsten",
      "inspector.light.preset.studio_tungsten", "Studio Tungsten 1000 W", 3200.0f, LightSourcePresetUnit::Lumens, 26000.0f},
    LightSourcePresetDefinition{LightSourcePreset::SunriseSunset, "sunrise_sunset",
      "inspector.light.preset.sunrise_sunset", "Sunrise/Sunset", 2500.0f, LightSourcePresetUnit::Lux, 400.0f},
    LightSourcePresetDefinition{LightSourcePreset::MiddaySun, "midday_sun",
      "inspector.light.preset.midday_sun", "Midday Sun", 5200.0f, LightSourcePresetUnit::Lux, 110000.0f},
    LightSourcePresetDefinition{LightSourcePreset::Daylight, "daylight",
      "inspector.light.preset.daylight", "Daylight (Sun + Sky)", 6000.0f, LightSourcePresetUnit::Lux, 120000.0f},
    LightSourcePresetDefinition{LightSourcePreset::Overcast, "overcast",
      "inspector.light.preset.overcast", "Overcast (Bright)", 7000.0f, LightSourcePresetUnit::Lux, 10000.0f},
    LightSourcePresetDefinition{LightSourcePreset::OutdoorShade, "outdoor_shade",
      "inspector.light.preset.outdoor_shade", "Outdoor Shade", 7500.0f, LightSourcePresetUnit::Lux, 20000.0f},
    LightSourcePresetDefinition{LightSourcePreset::BlueSky, "blue_sky",
      "inspector.light.preset.blue_sky", "Blue Sky", 9000.0f, LightSourcePresetUnit::Lux, 25000.0f},
};

/// Uses the existing SPOT flux convention, independent of its cone angles.
[[nodiscard]] double candela_solid_angle(LightType shape) noexcept {
  return shape == LightType::Spot ? std::numbers::pi
                                  : 4.0 * std::numbers::pi;
}

/// Avoids overflow when an extended physical-unit numeric input reaches float limits.
[[nodiscard]] float representable_intensity(double value) noexcept {
  constexpr double maximum = std::numeric_limits<float>::max();
  if (std::isnan(value)) {
    return 0.0f;
  }
  return static_cast<float>(std::clamp(value, -maximum, maximum));
}

/// Restricts picker presentation to displayable channels without changing storage.
[[nodiscard]] float display_channel(float value) noexcept {
  return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

} // namespace

std::span<const LightSourcePresetDefinition> light_source_presets() noexcept {
  return kLightSourcePresets;
}

LightEngineValues light_engine_values(const LightComponent &light) noexcept {
  const double energy = light.intensity > 0.0f
      ? static_cast<double>(light.intensity) / kLightReferenceSimpleIntensity
      : 0.0;
  const float physical = representable_intensity(
      energy * std::numbers::pi / kLightPreviewExposure);
  return {
      .preview_energy = representable_intensity(energy),
      .illuminance_lux = light.type == LightType::Directional ? physical : 0.0f,
      .luminous_intensity_candela = light.type == LightType::Directional
          ? 0.0f : physical,
  };
}

std::string_view light_source_preset_id(LightSourcePreset preset) noexcept {
  const auto index = static_cast<std::size_t>(preset);
  return index < kLightSourcePresets.size() &&
                 kLightSourcePresets[index].preset == preset
             ? kLightSourcePresets[index].id
             : std::string_view{};
}

bool light_component_metadata_is_valid(const LightComponent &light) noexcept {
  const bool valid_unit = light.light_unit == LightUnit::Simple ||
                          light.light_unit == LightUnit::Lumens ||
                          light.light_unit == LightUnit::Candela;
  const bool valid_mode = light.color_mode == LightColorMode::Color ||
                          light.color_mode == LightColorMode::ColorTemperature;
  return valid_unit && valid_mode &&
         std::isfinite(light.color_temperature_kelvin) &&
         light.color_temperature_kelvin >= kLightMinimumTemperatureKelvin &&
         light.color_temperature_kelvin <= kLightMaximumTemperatureKelvin &&
         !light_source_preset_id(light.source_preset).empty();
}

double light_intensity_for_unit(const LightComponent &light) noexcept {
  const double simple = light.intensity;
  switch (light.light_unit) {
  case LightUnit::Simple:
    return simple;
  case LightUnit::Lumens:
    return simple * 1000.0;
  case LightUnit::Candela:
    return simple * 1000.0 / candela_solid_angle(light.type);
  }
  return simple;
}

float light_intensity_from_unit(double value, LightType shape,
                                LightUnit unit) noexcept {
  switch (unit) {
  case LightUnit::Lumens:
    value /= 1000.0;
    break;
  case LightUnit::Candela:
    value = value / 1000.0 * candela_solid_angle(shape);
    break;
  case LightUnit::Simple:
    break;
  }
  return representable_intensity(value);
}

double light_directional_brightness(const LightComponent &light) noexcept {
  return static_cast<double>(light.intensity) * 100000.0;
}

float light_intensity_from_brightness(double value) noexcept {
  return representable_intensity(value / 100000.0);
}

QVec3 light_color_temperature_rgb(float kelvin) noexcept {
  const double temperature = std::isfinite(kelvin)
      ? std::clamp(kelvin, kLightMinimumTemperatureKelvin,
                   kLightMaximumTemperatureKelvin)
      : kLightDefaultTemperatureKelvin;
  // Krystek's rational approximation locates a black body in CIE 1960 (u, v).
  // See light_properties.md for the primary reference and rendering contract.
  const double squared = temperature * temperature;
  const double u = (0.860117757 + 0.000154118254 * temperature +
                    0.000000128641212 * squared) /
                   (1.0 + 0.000842420235 * temperature +
                    0.000000708145163 * squared);
  const double v = (0.317398726 + 0.0000422806245 * temperature +
                    0.0000000420481691 * squared) /
                   (1.0 - 0.0000289741816 * temperature +
                    0.000000161456053 * squared);
  // CIE 1960 to XYZ at Y=1, then the standard D65 XYZ-to-linear-sRGB matrix.
  const double x = 1.5 * u / v;
  const double z = (2.0 - 0.5 * u - 5.0 * v) / v;
  const double red = 3.2404542 * x - 1.5371385 - 0.4985314 * z;
  const double green = -0.9692660 * x + 1.8760108 + 0.0415560 * z;
  const double blue = 0.0556434 * x - 0.2040259 + 1.0572252 * z;
  const double peak = std::max({red, green, blue, 0.00001});
  return {static_cast<float>(std::clamp(red / peak, 0.0, 1.0)),
          static_cast<float>(std::clamp(green / peak, 0.0, 1.0)),
          static_cast<float>(std::clamp(blue / peak, 0.0, 1.0))};
}

QVec3 light_srgb_to_linear(QVec3 srgb) noexcept {
  return {srgb_to_linear(display_channel(srgb.x)),
          srgb_to_linear(display_channel(srgb.y)),
          srgb_to_linear(display_channel(srgb.z))};
}

QVec3 light_linear_to_srgb(QVec3 linear) noexcept {
  return {linear_to_srgb(display_channel(linear.x)),
          linear_to_srgb(display_channel(linear.y)),
          linear_to_srgb(display_channel(linear.z))};
}

bool apply_light_source_preset(LightComponent &light,
                               LightSourcePreset preset) noexcept {
  if (light_source_preset_id(preset).empty()) {
    return false;
  }
  light.source_preset = preset;
  if (preset != LightSourcePreset::Custom) {
    const auto &definition = kLightSourcePresets[static_cast<std::size_t>(preset)];
    light.color_temperature_kelvin = definition.temperature_kelvin;
    light.color_mode = LightColorMode::ColorTemperature;
    light.color = light_color_temperature_rgb(definition.temperature_kelvin);
    // Compress nominal source differences into useful preset starting values.
    // One doubling changes four slider units; medium lamps/daylight center at 16.
    const double reference = definition.unit == LightSourcePresetUnit::Lux
        ? 120000.0 : 800.0;
    light.intensity = static_cast<float>(std::clamp(
        kLightReferenceSimpleIntensity + 4.0 * std::log2(definition.output / reference),
        static_cast<double>(kLightMinimumSimpleIntensity),
        static_cast<double>(kLightMaximumSimpleIntensity)));
  }
  return true;
}

} // namespace quader
