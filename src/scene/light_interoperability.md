<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Shared light scale and engine output

The implemented authoring contract uses one nonnegative **Intensity** numeric input for
Directional, Point, and Spot, with **16** as the new-light default and reference intensity.
There is no intensity slider or artificial upper cap; finite float storage is the numeric
limit. Physical quantities are derived backend values,
not alternate Inspector units. [Light properties](light_properties.md) owns preset references,
color temperature, and storage compatibility; [light_engine_values()](light_properties.hpp)
is the common scene-owned conversion used by rendering and engine adapters.

## Current mapping

For stored intensity `I`, preview energy is `G = I / 16`. The fixed reference camera uses
f/16, shutter 0.008 seconds, and ISO 100. Its Filament exposure is `1 / 38400`. The common
physical output is `P = G * pi / exposure`, before float saturation:

| Intensity | Preview energy G | Directional output | Point / Spot output |
| ---: | ---: | ---: | ---: |
| 1 | 0.0625 | 7539.822 lux | 7539.822 candela |
| 16 | 1 | 120637.158 lux | 120637.158 candela |
| 32 | 2 | 241274.316 lux | 241274.316 candela |

These are reference examples, not control bounds. Entering **150000** preserves that number
and resolves to preview energy **9375** through the same mapping.

The unused physical quantity is zero: Directional has no candela output, and Point/Spot have
no directional-lux output. Legacy unit metadata does not enter this calculation. Filament
receives Directional lux; its ordinary builder inputs for Point and Spot are respectively
`4*pi*P` lumens and `pi*P` reference lumens, yielding the same candela. See
[render policy](../render/light_render_policy.hpp).

At the neutral diffuse reference with perpendicular lighting and viewing, physical output
times exposure divided by pi equals G for both Lambert and Burley.
This defines the energy baseline for Godot/URP-style scalar adapters. It does not promise
identical final pixels across engines: attenuation, BRDF, environment, tone mapping, and
color management remain relevant. New Quader scenes use Linear tone mapping; AgX is an
additional serialized option at value 5. Existing tone-mapper IDs and stored selections stay
valid. The reference camera is fixed for rendering. Historical camera fields round-trip,
but changing them does not change the effective exposure.

Materials default to Burley, including older materials without a saved diffuse-model choice.
Lambert remains an explicit material option. The Godot bridge transfers that choice for new
materials and when material refresh is enabled; ordinary reuse preserves Godot-side edits.
The native Burley path uses perceptual roughness to agree with Godot's convention while
retaining Filament's standard specular and indirect-lighting paths. This model choice does
not change the authored light scale.

An ACES label does not establish the same output curve across engines. Godot and Filament
use different ACES approximations, so a global intensity adjustment cannot align them across
all input brightness levels. Controlled comparisons use matching Linear or AgX settings;
Quader's AgX reference uses contrast 1.25 and white 16.29. Existing scene selections remain
authored choices.

Source presets compress nominal source ratings into bounded starting intensities with
`clamp(16 + 4*log2(output/reference), 1, 32)`. Lamp references use 800 lumens and outdoor
references use 120000 lux. The 1-32 interval applies only when choosing a preset; manual
numeric entry has no such cap. Every Shape receives the same preset intensity and RGB.
These ratings describe the source audit; they are not the current rendered light's emitted flux.

## Compatibility and calibration boundaries

The 2026-09-08 Vulkan comparison used patched Filament 1.71.6 revision 4 and Godot
4.7 stable on an RTX 4090. It imported actual bridge lights and materials and checked
76 pairs with Linear and AgX: all three shapes at strengths 1, 16 and 32 with three
neutral Burley materials, representative Lambert controls, and grazing dielectric
and metallic cases for both models. All 56 normal-view cases differed by at most
one 8-bit display level. The largest sampled difference was seven levels near
clipped white with Linear; the same case matched exactly with AgX. This is consistent
with Filament's color-grading LUT interpolation across Linear's clipping boundary;
the underlying HDR input was not measured. Metallic grazing highlights differed by up to four levels with
Linear and three with AgX, identically for both diffuse models; the engines retain
different specular visibility and DFG approximations.

Eighteen native checks confirmed identical pixels after editing or disabling stored
photographic exposure. Same-material model switches restored the original pixels,
and metallic controls were invariant to the diffuse selection. These checks use no
environment lighting or shadows, range 12, falloff 1, and matched material settings;
they do not establish parity for arbitrary materials, environments, post effects,
or other engines. In particular, they do not measure the effect of differing ACES
curves in a user's complete scene.
The maintained native `RenderColorRuntimeTests` and Godot
`tests/run-light-calibration.ps1` own this comparison.

Existing valid intensity numbers are retained without an artificial cap and evaluated
with the same new mapping. The approved change intentionally changes their rendered brightness.
There is no legacy gain or migration that preserves the previous image. Opening or saving
never reapplies a preset, changes stored RGB, or clamps valid historical intensity values.
Metadata and source identity continue through public APIs, duplication, undo/redo, and QDR.

The former Quader mapping used `100000*I` directional lux and `10000000*I` Point/Spot builder
lumens, including a punctual-only gain of 10000. The earlier physical-unit Inspector proposal,
kilolux display, and editable/automatic exposure proposal are superseded by the common scale
and fixed reference camera. Historical ACES/daylight measurements describe that earlier
configuration and are not validation of the current Linear reference.

New lights retain artistic Falloff 1. An inverse-square physical light corresponds to
Falloff 2; changing the exponent changes the distance response and cannot be reconciled by
one intensity multiplier. Range cutoff, spot edge attenuation, and engine geometry scale also
need explicit agreement. For a controlled physical comparison, use one world unit per meter,
Falloff 2, matched RGB/materials, no environment, and locked exposure. Compare directional
angles, Point distances 1/2/4, and Spot center/edge before comparing tone-mapped editor views.

The common helper provides backend quantities and a neutral energy target. Engine-specific
rendered parity still needs controlled measurements. Preserve the computed linear RGB when
transferring a temperature color, respecting the target API's color-space convention; avoid
applying a second temperature multiplier. Numeric equality alone is not an appearance test.

## Source audit

The [upstream source audit](../../thirdparty/light-engine-sources.md) records the inspected
Unreal, Blender, Unity, and Filament versions with exact source locations and pinned links.
Its findings establish quantity conventions; they do not establish rendered image parity.

## Interpreting physical backend values

Let E and J be the helper's directional lux and Point/Spot candela. These destination-specific
physical conversions were established by the source audit; they are separate from an
appearance-calibrated scalar adapter and do not change the Inspector's numeric intensity.

| Destination | Directional | Point / Spot |
| --- | --- | --- |
| Filament 1.71.6 | E lux | J candela; equivalent builder inputs are Point `4*pi*J` lumens and ordinary Spot `pi*J` reference lumens. |
| Unreal 5.8, physical inverse-square path | Intensity E | Candela intensity J; convert world meters to UE centimeters for geometry/range. Do not duplicate UE's native local-light factor. |
| Unity HDRP 17.6 | Native intensity E lux | Native intensity J candela, through the supported API. |
| Blender normalized lights, glTF SPEC convention | Sun Strength `E/683` W/m2 | Point/Spot Power `4*pi*J/683` W, light exposure 0, with source RGB preserved. |
| Unity URP / Built-in | Use the shared G baseline with the adapter's specified pipeline/profile | Shared G plus compatible distance/cone attenuation; physical units alone are insufficient. |

Blender's 683 factor is an explicit peak-efficacy interchange convention, not a universal
spectral watts-to-lumens identity or an electrical wattage conversion. Spot flux conventions
also differ across engines: transferring J, angles, and attenuation avoids treating each
engine's lumen field as equivalent. Candela is not nits; nits require an emitting area.

The [physical conversion evidence](../../thirdparty/light-engine-sources.md#physical-conversion-evidence)
records the supporting engine source locations and interchange conventions.

`LightPropertiesTests` protects the common scale, shape/unit independence, preset bounds,
and stored-number compatibility. Render-policy and GPU tests protect Quader's backend use;
source citations alone do not establish cross-engine image parity.
