////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/components.hpp"

#include <span>
#include <string_view>

namespace quader {

inline constexpr float kLightMinimumTemperatureKelvin = 1500.0f;
inline constexpr float kLightMaximumTemperatureKelvin = 15000.0f;
inline constexpr float kLightDefaultTemperatureKelvin = 6500.0f;
// Preset starting values only; authored intensity has no artistic upper bound.
inline constexpr float kLightMinimumSimpleIntensity = 1.0f;
inline constexpr float kLightMaximumSimpleIntensity = 32.0f;
inline constexpr float kLightReferenceSimpleIntensity = 16.0f;
inline constexpr float kLightPreviewAperture = 16.0f;
inline constexpr float kLightPreviewShutterSeconds = 1.0f / 125.0f;
inline constexpr float kLightPreviewSensitivity = 100.0f;
inline constexpr double kLightPreviewExposure = 1.0 / 38400.0;
inline constexpr float kLightMinimumFalloff = 0.0f;
inline constexpr float kLightMaximumFalloff = 2.0f;

/// Physical quantity provided by a source reference, not a display-unit choice.
enum class LightSourcePresetUnit { ColorOnly, Lumens, Lux };

/// Nominal source settings; labels are localization keys owned by the UI catalog.
struct LightSourcePresetDefinition {
  LightSourcePreset preset;
  std::string_view id;
  std::string_view label_key;
  std::string_view fallback_label;
  float temperature_kelvin;
  LightSourcePresetUnit unit;
  float output;
};

/// Engine-neutral output resolved from the single artist-facing intensity.
/// Preview energy matches a neutral Godot light; physical values allow
/// physical renderers to reproduce it with the fixed preview exposure.
struct LightEngineValues {
  float preview_energy = 0.0f;
  float illuminance_lux = 0.0f;
  float luminous_intensity_candela = 0.0f;
};

/// Resolves the common scale without camera settings or legacy unit metadata.
[[nodiscard]] LightEngineValues
light_engine_values(const LightComponent &light) noexcept;

/// Returns the immutable catalog, including Custom as its first entry.
[[nodiscard]] std::span<const LightSourcePresetDefinition>
light_source_presets() noexcept;

/// Returns a stable serialized/debug identity, or an empty view for invalid IDs.
[[nodiscard]] std::string_view
light_source_preset_id(LightSourcePreset preset) noexcept;

/// Validates authoring metadata without clamping historical intensity or falloff.
[[nodiscard]] bool
light_component_metadata_is_valid(const LightComponent &light) noexcept;

/// Historical unit conversion retained for compatibility; unused by current controls.
[[nodiscard]] double
light_intensity_for_unit(const LightComponent &light) noexcept;

/// Inverse historical unit conversion; current authoring edits intensity directly.
[[nodiscard]] float light_intensity_from_unit(double value, LightType shape,
                                              LightUnit unit) noexcept;

/// Historical directional conversion; engine adapters use light_engine_values.
[[nodiscard]] double
light_directional_brightness(const LightComponent &light) noexcept;

/// Inverse historical directional conversion; not the current engine calibration.
[[nodiscard]] float light_intensity_from_brightness(double value) noexcept;

/// Returns a normalized linear-sRGB Planckian-locus color for 1500-15000 kelvin.
/// Uses Krystek's CIE 1960 approximation; nonfinite input uses remembered default.
[[nodiscard]] QVec3 light_color_temperature_rgb(float kelvin) noexcept;

/// Converts the light picker's sRGB display color into scene-owned linear RGB.
[[nodiscard]] QVec3 light_srgb_to_linear(QVec3 srgb) noexcept;

/// Converts scene-owned linear RGB to the light picker's sRGB display color.
[[nodiscard]] QVec3 light_linear_to_srgb(QVec3 linear) noexcept;

/// Applies a preset atomically to a candidate component; Custom only clears its ID.
/// Leaves shape, display unit, range, falloff, and shadow settings unchanged.
/// Source ratings select a bounded artist intensity shared by every shape.
[[nodiscard]] bool apply_light_source_preset(LightComponent &light,
                                             LightSourcePreset preset) noexcept;

} // namespace quader
