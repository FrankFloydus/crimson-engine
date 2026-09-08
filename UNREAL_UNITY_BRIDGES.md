<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Future Unreal and Unity bridges

Planning reference requested 2026-09-07, linked from the [task list](tasklist.md).
Neither bridge is implemented or scheduled by this document. It records the lighting
research and Godot integration lessons for future development. Select and pin engine and
render-pipeline versions before implementation; recheck their APIs and shader behavior then.

The [2026-09-08 light source audit](src/scene/light_interoperability.md) records verified Unreal
5.8, Blender 5.2 alpha, Unity HDRP/URP and pinned Filament internals. Quader now implements one
nonnegative numeric intensity control for every light shape, fixed preview exposure, and ABI 5
resolved light values. Intensity has no artistic upper cap or slider. The Unreal and Unity
adapters below remain future work; neither bridge exists yet.

## Intended result and scope

Build editor integrations that import saved Quader scenes and synchronize completed saves,
including ordinary geometry/material workflows and optional light import. Reuse the native
reader and established publication contracts. This brief focuses on lighting feasibility;
geometry, material, texture and collision mappings need their own engine-specific validation.

The existing source data is sufficient for an initial light importer. Exact visual matching
and arbitrary distance attenuation are separate acceptance goals. Start with the standard
inverse-square case and report unsupported behavior explicitly.

## Shared data and ownership

Use the [native bridge reader contract](src/io/bridge/README.md) and
[C ABI header](src/io/bridge/quader_bridge_reader.hpp) as the import boundary. Keep QDR
decoding, project containment, validation and saved-byte fingerprints in their current native
owners. Unreal and Unity adapters should not implement independent QDR parsers.

ABI 5 currently supplies:

| Input | Future bridge use |
| --- | --- |
| Project UUID, scene UUID and light entity ID | Stable identity for import, reimport and preservation |
| Name and Directional/Point/Spot type | Editor object/component creation |
| Affine world transform and effective visibility | Placement, emission direction and enabled state |
| Linear RGB and authored intensity | Color transfer and source authoring state |
| `preview_energy`, `illuminance_lux`, `luminous_intensity_candela` | Central resolved calibration; consume the appropriate field without repeating Quader's conversion |
| Range, distance attenuation and cast-shadow state | Punctual influence and shadow configuration |
| Fixed-reference aperture, shutter seconds and ISO | Document the preview baseline; not an imported authored camera |

The ABI always reports `(16, 1/125, 100)`: Filament's reference exposure `p0 = 1/38400`.
Authored photographic values and the old exposure toggle remain stored in Quader scenes, but
do not change current viewport lighting or these reader fields. There is no exposure-bypass
branch and no separate legacy light-calibration profile. Saved scalar values are retained;
the current resolver interprets them on the same scale as new lights. Faithful authored-camera
import would require a deliberate ABI extension. Do not infer authored camera state from the
fixed reference triple.

Directional and spot emission follows transformed local **-Y**. Convert coordinate basis,
handedness and distance units explicitly. Use the same placement conversion for geometry and
lights, and remove inappropriate inherited scale/shear from the light orientation.

Reader strings and arrays are borrowed. Respect handle lifetime, ABI/struct-size checks,
threading rules and matching reader/consumer packaging. The current reader is a useful base,
not a claim that all geometry conventions are already suitable for every engine.

## Mapping and renderer differences

| Quader | Unreal | Unity |
| --- | --- | --- |
| Directional | Directional Light | Light with Directional type |
| Point | Point Light | Light with Point type |
| Spot | Spot Light | Light with Spot type |
| Punctual range | Attenuation Radius | Range |
| Color, visibility, cast shadows | Supported through light/component APIs | Supported through Light/GameObject APIs |
| Intensity | Physical units or unitless, depending on configuration | HDRP physical units; separate calibration for URP and Built-in |
| Distance attenuation 2 | Enable inverse-square falloff | Standard inverse-square path in URP/HDRP; Built-in differs |
| Other distance exponents | No direct equivalent in the standard falloff-exponent control | No general matching per-light exponent in the standard Light API |

Property references: [Unreal Point Lights](https://dev.epicgames.com/documentation/en-us/unreal-engine/point-lights-in-unreal-engine),
[Unity Light API](https://docs.unity3d.com/6000.3/Documentation/ScriptReference/Light.html),
[Unity pipeline falloff differences](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/birp-onboarding/birp-light-falloff-in-urp.html).

### Distance attenuation

Quader's exponent controls distance decay independently of range. Away from its near-light
clamp, the punctual profile is:

```text
attenuation(d) = d^(-exponent) * max(1 - (d / range)^4, 0)^2
```

New lights default to exponent **1**, selecting inverse distance. Historical QDR's absent
falloff field defaults to **2**, preserving that stored inverse-square setting. The Inspector
permits 0–2; exponent 0 removes distance decay, and older saved values outside that interval
remain stored. Negative historical values increase intensity with distance before the range
cutoff. Exact near-source clamps and cutoff behavior remain part of validation, even when
two engines both call their model inverse-square. Initial physical parity tests must explicitly
set exponent 2; the new-light default is not automatically equivalent to that profile.

Unreal's **Light Falloff Exponent** is used when inverse-square falloff is disabled and has
different semantics. Epic describes value 2 in that mode as almost linear. Never copy Quader's
exponent into it as an equivalent setting. For arbitrary exponents, investigate and choose
between an explicitly documented approximation and custom rendering support; do not silently
discard the source value. [Unreal attenuation API](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/PointLightComponent).

Unity requires separate Built-in, URP and HDRP support decisions. Its documented URP custom
falloff workflow edits pipeline HLSL, with separate handling for baked lighting. Per-light
Quader exponents would additionally need suitable per-light data delivery and versioned
pipeline integration. HDRP's range-attenuation control concerns the range window; it does not
provide Quader's arbitrary distance exponent.
[URP customization](https://docs.unity3d.com/6000.0/Documentation/Manual/urp/lighting/custom-lighting-change-light-falloff.html),
[Unity attenuation shader source](https://github.com/Unity-Technologies/Graphics/blob/master/Packages/com.unity.render-pipelines.core/ShaderLibrary/CommonLighting.hlsl).

### Intensity, spotlights and exposure

The [scene-owned resolver](src/scene/light_properties.cpp) defines all shapes from the same
artist-facing intensity `I`, with a new-light default of 16. Its numeric field accepts
nonnegative representable values, including values above 32:

```text
G = max(I, 0) / 16
p0 = 1 / 38400
Directional: E = G * pi / p0 lux
Point/Spot: J = G * pi / p0 candela
```

Thus `I = 16` gives Godot preview energy `G = 1`; the corresponding physical field is
approximately 120637.16 lux for Directional or 120637.16 candela for Point/Spot. These are
calibrated backend quantities derived from the artist control, not a claim that the scalar
itself is lux, candela, or a lamp's measured output. The physical field unused by the shape is
zero. Presets supply editable starting values in 1–32; that interval does not limit manual
intensity entry. Historical scalar values remain stored; consumers use the resolver's published
fields, including its finite-range saturation, instead of imposing a separate cap on import.

The [Filament adapter](src/render/light_render_policy.hpp) sends `E` directly and converts
`J` to Point `4*pi*J` lumens or ordinary Spot `pi*J` reference lumens. The old independent
punctual multiplier is gone. Godot's nonphysical import consumes `preview_energy` directly;
its physical mode uses the physical fields. Unreal and Unity must use their own unit contracts:

| Destination path | Directional | Point/Spot |
| --- | --- | --- |
| Unreal, inverse-square local lights | Set Intensity to reader `illuminance_lux` | Select Candela and set Intensity to reader `luminous_intensity_candela` |
| Unity HDRP | Set native lux from `illuminance_lux` | Set native candela from `luminous_intensity_candela` through the supported HDRP API |
| Unity URP / Built-in | Evaluate `preview_energy` against a locked pipeline-specific reference | Calibrate `preview_energy`, distance and cone response together; parity is not yet implemented or measured |

Convert geometry and ranges to Unreal centimeters together. Unreal's local-light implementation
already applies its centimeter-squared factor internally; do not multiply reader candela by
10000 in the exporter. HDRP and Unreal receive resolved physical quantities without multiplying
by Quader's reference exposure. Their destination camera applies exposure during rendering.

Unreal and HDRP provide physical units, but equal lumen values alone do not establish equal
spotlight brightness. Validate cone solid-angle normalization and reflector behavior; candela
is the shared reader quantity for Point/Spot. Unreal lumen conversion uses
`2*pi*(1-cos(outerHalfAngle))`; HDRP's cone/reflector choice also affects lumen conversion.
Importing reader candela avoids an unnecessary and potentially different lumen round trip.
[Unreal physical units](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-physical-lighting-units-in-unreal-engine),
[HDRP physical units](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Physical-Light-Units.html).

Quader's current spot inner/outer cones are fixed. Unreal and Unity URP/HDRP support separate
inner and outer controls. Convert full-angle versus half-angle conventions explicitly, and
verify the edge profile. Editable Quader cone angles and corresponding reader fields would
be useful future additions; ABI 5 does not expose them.
[Unreal spotlights](https://dev.epicgames.com/documentation/en-us/unreal-engine/spot-lights-in-unreal-engine),
[HDRP spotlight controls](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-light-component.html).

Choose an explicit exposure policy before calibrating energy:

- Preserve the reader's resolved physical quantities and let the destination camera/environment
  control exposure. This transfers the calibration without promising identical final pixels.
- Or match Quader's viewport against a documented, locked destination exposure and tone-mapping
  baseline. Derive any appearance correction from the source and destination linear responses.

Quader's baseline is fixed at `p0`; stored camera edits or the old toggle do not change it.
`preview_energy` already represents the preview calibration, while `E` and `J` are backend
quantities intended to be rendered through the reference exposure. Do not multiply either
reader branch by another source-exposure factor. Keep destination cameras, automatic exposure,
and project settings unchanged unless their import is explicitly enabled.

For controlled physical comparisons, match the destination exposure multiplier to `p0`.
Unreal's default lens attenuation differs from Filament's factor of 1.2. In Unreal postprocess
Manual mode, with physical-camera exposure enabled and no editor fixed override or bias curve,
the same camera settings require absolute Exposure Compensation `-log2(1.2)`, approximately
-0.263 stops. The editor fixed-EV override ignores that compensation; use the source EV100
plus 0.263 instead. HDRP's reference formula uses the same 1.2 factor as Filament. Automatic
exposure and tone mapping can still counteract or alter the visible intensity comparison.
[Unreal exposure](https://dev.epicgames.com/documentation/unreal-engine/auto-exposure-in-unreal-engine),
[HDRP exposure](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Override-Exposure.html).

## API and source access

Normal importing, property assignment, synchronization and Undo can use public editor APIs;
full engine source access is not a prerequisite. Renderer source becomes useful for diagnosing
differences and implementing custom shading behavior.

Unreal source is available through Epic's licensed access process. Unity's native C++ engine
is not generally public, but its URP/HDRP rendering code and shader libraries are publicly
available. Relevant lighting math can therefore be inspected and pipeline shaders customized
without Unity native engine access. Source access does not remove the maintenance cost of
carrying custom rendering changes across versions.
[Unreal source access](https://dev.epicgames.com/documentation/en-us/unreal-engine/downloading-source-code-in-unreal-engine),
[Unity Graphics repository](https://github.com/Unity-Technologies/Graphics).

## Future work

- [ ] Select Unreal version, Unity version, supported Unity pipelines and initial platforms.
- [ ] Build minimal editor integrations using the shared reader: an Unreal editor plugin and
  Unity editor tooling with a native-reader interop boundary. Keep importing out of runtime builds.
- [ ] Establish basic geometry/material import and coordinate/unit conversion fixtures in each
  engine before judging lighting parity.
- [ ] Implement Directional/Point/Spot import with exponent 2; choose physical-unit, exposure,
  color-temperature and engine-only-property defaults explicitly.
- [ ] Port import/preservation behavior: persisted and undoable **Import lights** and
  **Always override lights**, initially off. Import off retains existing lights; import on with
  override off preserves existing edits, adds new source lights and removes source-deleted lights;
  override on reapplies saved source data under a documented replacement policy.
- [ ] Preserve edits made during background loading by resolving existing identities at
  publication. Define unmanaged moved lights, retained hierarchy/world transforms and Undo.
- [ ] Reuse completed-save fingerprints and atomic publication: failure, cancellation or a
  superseded candidate retains the previous import. Watcher workers must not mutate editor objects.
- [ ] Decide custom-exponent support separately for Unreal and each Unity pipeline. Document
  supported values, approximations and any shader/pipeline maintenance requirements.
- [ ] Evaluate editable spotlight cones and a versioned ABI extension if the selected scope needs
  authored camera import or additional light controls.

Use the [existing Godot save-sync contract](../quader-godot/docs/save-sync.md) as a behavior
reference, while implementing transactions, assets and Undo through each destination editor.

## Acceptance and visual review

Begin with small controlled fixtures and a quick visual handoff before expanding coverage:

- [ ] Fixed camera, identical scale/geometry/materials, no environment illumination, controlled
  exposure and linear tone mapping. Measure point-light falloff at known distances and range edges.
- [ ] Validate directional and spotlight orientation, cone edges, linear RGB conversion, physical
  intensity normalization, source visibility and shadows.
- [ ] Compare 2, 1, 0 and negative exponents only where supported; expose unsupported cases clearly.
- [ ] Verify I=0/1/16/32/64 resolves to preview energy 0/0.0625/1/2/4 for every shape, with the expected
  physical reader fields. Save/reopen and confirm stored camera edits and the old exposure toggle
  do not alter the fixed source calibration. Test destination exposure policies separately.
- [ ] Verify actual editor viewport presentation as well as offscreen render targets. Quader's
  double-sRGB bug made correct attenuation appear wrong: a grading LUT and sRGB swapchain must
  not both encode the same output. Equal tone-mapper names also do not guarantee equal curves.
- [ ] After visual feedback, add permanent coverage for old-scene defaults, persistence, reader
  lifetime, synchronization, all checkbox states, preservation during loading, Undo/Redo,
  cancellation and failures. Validate packaging and an existing destination development project
  without replacing user assets or editor settings. Apply the current repository regression gates.

The initial deliverable is reproducible import behavior and measured agreement under controlled
conditions. Any remaining renderer, shadow, material, tone-mapping or indirect-light differences
must be described with their tested scope rather than hidden in unexplained energy multipliers.
