<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Light Export Calibration Internals

This is an internal architecture note for future game-engine bridge/export work. It records the
current lighting-unit decision so Quader can be used as a lighting-shape design tool without
pretending that every target engine renders identical pixels.

Current code, tests, engine versions, and active export requirements are authoritative. Update this
file when an export profile, target engine version, or Quader light authoring model changes.

## Goal

Quader should keep one stable authoring model for lights, then convert through explicit target
profiles for Unity, Godot, Unreal, and future bridges.

Do not export renderer implementation values. In particular, the current Filament viewport
compensation for point/spot lights is only a Quader viewport calibration detail and must not be used
as an interchange value.

## Canonical Quader Light Units

Use Quader light properties as the source of truth:

- Point and spot lights:
  - user-facing `intensity = 1.0`
  - canonical luminous flux: `1000.0 lm`
  - formula: `lumens = intensity * 1000.0`
- Directional lights:
  - user-facing `intensity = 1.0`
  - canonical illuminance: `100000.0 lx`
  - formula: `lux = intensity * 100000.0`
- Common data:
  - `rangeMeters`
  - linear color and optional temperature in Kelvin
  - source radius in meters
  - spot inner and outer angles in degrees
  - shadow state and future shadow profile fields

The current Filament viewport path maps point/spot `1.0` to `10,000,000` Filament lumens:
`1000 lm` base times `10000x` viewport exposure compensation. That value belongs only in
`LightRenderableCache` or a future viewport lighting profile, not in export payloads.

## Initial Export Profiles

Physical pipelines should receive physical units directly.

```json
{
  "engine": "Unity",
  "pipeline": "HDRP",
  "profileVersion": 1,
  "point": {"unit": "lumen", "multiplier": 1000.0},
  "spot": {"unit": "lumen", "multiplier": 1000.0},
  "directional": {"unit": "lux", "multiplier": 100000.0},
  "range": {"unit": "meter", "multiplier": 1.0}
}
```

Recommended starting profiles:

- Unity HDRP:
  - Point/Spot: export lumens.
  - Directional: export lux.
  - Range: export meters.
  - Store Quader metadata for round-trip even when the target file already stores physical units.
- Godot with physical light units enabled:
  - Omni/Spot: export lumens.
  - Directional: export lux.
  - Energy should stay `1.0` unless the bridge intentionally uses Godot Energy as a secondary
    artistic multiplier.
  - Export project/environment/camera notes because Godot physical light units also require exposure
    setup.
- Unreal with inverse-squared falloff enabled:
  - Point/Spot: prefer lumen units.
  - Directional: export lux.
  - Range/attenuation radius: export meters after applying scene scale.
- Unity URP/Built-in or other non-physical modes:
  - Treat intensity as unitless.
  - Start with `targetIntensity = quaderIntensity * profileMultiplier`.
  - Keep physical Quader metadata beside the exported light so later re-export and round-trip are
    stable.

## Calibration Harness

Physical unit support does not guarantee identical rendered pixels. Tonemapping, exposure, falloff
clamps, shadow filters, color management, and material BRDF differences will still diverge.

Build a small calibration harness per target pipeline:

1. Use a fixed scene scale where one Quader unit equals one meter.
2. Disable ambient, GI, reflection probes, bloom, auto exposure, and engine defaults that add hidden
   light unless the target profile explicitly includes them.
3. Use a fixed camera, fixed exposure, fixed tonemapping, and a neutral gray matte material.
4. Render reference cases:
   - point light above a gray plane at known heights;
   - spot light with known outer/inner angles;
   - directional light at known angle;
   - range cutoff/falloff tests;
   - optional shadow softness and bias cases.
5. Sample fixed image patches in linear color.
6. Fit per-target multipliers and falloff compensation:
   - `pointLumensToIntensity`
   - `spotLumensToIntensity`
   - `directionalLuxToIntensity`
   - `rangeMetersToEngineRange`
   - `sourceRadiusMetersToEngineRadius`
7. Save the fitted values as versioned export profiles.

Example non-physical profile shape:

```json
{
  "engine": "Unity",
  "pipeline": "URP",
  "engineVersion": "6000.1",
  "profileVersion": 1,
  "pointLumensToIntensity": 0.001,
  "spotLumensToIntensity": 0.001,
  "directionalLuxToIntensity": 0.00001,
  "rangeMetersToRange": 1.0,
  "notes": "Fitted with fixed exposure, no GI, neutral tone mapping."
}
```

## Export Metadata

Every bridge should preserve Quader light metadata when the target format allows it:

```json
{
  "quaderLight": {
    "type": "point",
    "intensityScalar": 1.0,
    "lumens": 1000.0,
    "rangeMeters": 10.0,
    "colorLinear": [1.0, 1.0, 1.0],
    "profile": "unity-hdrp-physical-v1"
  }
}
```

This prevents lossy round-trips when exporting to unitless pipelines or when target engines clamp,
rename, or remap light units.

## Source Notes

- Unity HDRP Light component reference: point and spot lights can use Lumen, Candela, Lux, and
  EV100; directional lights use Lux; range is expressed on the light component.
  <https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition%4017.0/manual/reference-light-component.html>
- Unity URP Light component reference: intensity is a brightness value, with default intensity
  `0.5` for Directional and `1` for Point/Spot/Area lights.
  <https://docs.unity3d.com/Packages/com.unity.render-pipelines.universal%4013.1/manual/light-component.html>
- Godot physical light and camera units: physical light units are disabled by default; when enabled,
  DirectionalLight3D uses lux and OmniLight3D/SpotLight3D use lumens, with camera/environment
  exposure requirements.
  <https://docs.godotengine.org/en/stable/tutorials/3d/physical_light_and_camera_units.html>
- Unreal physical lighting units: Directional Light uses lux; Point, Spot, and Rect lights can use
  Candela, Lumen, or Unitless when inverse-squared falloff is enabled.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/using-physical-lighting-units-in-unreal-engine>

## Rules

- Keep Quader authoring values physical and stable.
- Keep viewport renderer compensation local to the viewport renderer.
- Put every target-engine difference in an explicit export profile.
- Version export profiles by engine, render pipeline, and engine version.
- Prefer physical target units when the engine and pipeline support them.
- For unitless targets, use fitted multipliers and preserve Quader metadata.
- Do not promise pixel-perfect parity across engines; target approximate lighting shape, relative
  brightness, falloff behavior, and shadow intent.
