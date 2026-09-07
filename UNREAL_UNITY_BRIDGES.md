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

ABI 4 currently supplies:

| Input | Future bridge use |
| --- | --- |
| Project UUID, scene UUID and light entity ID | Stable identity for import, reimport and preservation |
| Name and Directional/Point/Spot type | Editor object/component creation |
| Affine world transform and effective visibility | Placement, emission direction and enabled state |
| Linear RGB and authored intensity | Engine-specific color and energy conversion |
| Range, distance attenuation and cast-shadow state | Punctual influence and shadow configuration |
| Effective camera aperture, shutter seconds and ISO | Source exposure calculation |

The ABI exposes effective exposure inputs, not a complete authored camera. Disabled Quader
camera exposure yields `(1, 1.2, 100)`, equivalent to multiplier 1. The separate toggle and
inactive authored photographic values remain in Quader's scene and are not exposed by ABI 4.
Faithful camera import would require a deliberate ABI extension. Do not infer the toggle from
that triple, since an enabled camera can have the same values.

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

Default 2 preserves the original Filament inverse-square calculation. Values 0 and 1 remove
distance decay or select inverse distance respectively; negative values increase intensity
with distance before the range cutoff. Exact near-source clamps and cutoff behavior remain
part of validation, even when two engines both call their model inverse-square.

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

Current [Quader light policy](src/render/light_render_policy.hpp) converts nonnegative authored
intensity `I` to `I * 100000` lux for directional lights and `I * 10000000` lumens for punctual
lights. Recheck this owner when implementing a bridge. Godot's additional conversion factors
must not be copied to another engine without checking that engine's normalization.

Unreal and HDRP provide physical units, but equal lumen values alone do not establish equal
spotlight brightness. Validate cone solid-angle normalization and reflector behavior; candela
may offer a clearer comparison after converting Filament's emitted intensity.
[Unreal physical units](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-physical-lighting-units-in-unreal-engine),
[HDRP physical units](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/Physical-Light-Units.html).

Quader's current spot inner/outer cones are fixed. Unreal and Unity URP/HDRP support separate
inner and outer controls. Convert full-angle versus half-angle conventions explicitly, and
verify the edge profile. Editable Quader cone angles and corresponding reader fields would
be useful future additions; ABI 4 does not already expose them.
[Unreal spotlights](https://dev.epicgames.com/documentation/en-us/unreal-engine/spot-lights-in-unreal-engine),
[HDRP spotlight controls](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-light-component.html).

Choose an explicit exposure policy before calibrating energy:

- Preserve physical light quantities and let the destination camera/environment control exposure.
- Or apply Quader's effective exposure to imported energy for a documented destination exposure
  baseline. The destination's exposure still affects the resulting image.

Honor the Quader bypass in the second policy. Avoid applying source exposure twice, and keep
destination cameras, automatic exposure and project settings unchanged unless their import is
explicitly enabled. Quader's Filament exposure uses a factor of 1.2; equal aperture/shutter/ISO
values in another engine are not proof of equal exposure multipliers.
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
- [ ] Toggle source exposure off/on, save/reopen and verify the selected destination exposure policy.
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
