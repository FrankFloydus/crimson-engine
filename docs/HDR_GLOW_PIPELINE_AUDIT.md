# HDR, color-space and Glow audit

This audit follows the October 2026 reports of broad white/yellow Glow around
partially covered lights and bright surfaces. It compares the canonical shared
renderer with the local Godot development checkout, then traces import,
authoring, texture storage, lighting, image effects and native presentation.

## Godot reference and resulting controls

Godot's Environment defaults are Glow disabled, Screen blend, intensity .3,
HDR threshold 1, HDR scale 2, luminance cap 12 and independent level gains
`0, .8, .4, .1, 0, 0, 0`. Normalization is disabled and the optional Bloom
contribution is zero. The reference is `scene/resources/environment.h` and
`environment.cpp` in the supplied Godot source, plus
`servers/rendering/renderer_rd/shaders/effects/copy.glsl`. The public
[Environment reference](https://docs.godotengine.org/en/stable/classes/class_environment.html)
documents these controls.

Crimson now uses those intensity, blend, threshold, scale, cap and level defaults
for new/reset settings. HDR Scale replaces Threshold Softness. Increasing it
widens the transition above Highlight Threshold; reducing HDR Luminance Cap
reduces the extracted halo source without altering scene radiance. Zero scale
is a hard threshold; zero cap omits filtering. The cap bounds RGB channels,
matching Godot's implementation despite its luminance name.

After the first Karis filter and one exposure multiplication, extraction is:

```text
G = min(C * smoothstep(threshold, threshold + scale, maxRGB(C)), cap)
```

The zero-scale case is explicit. Values at or below the default threshold 1
produce no Glow. The replaced excess/soft-knee response contributed .125 at
white with its default knee; the new response contributes zero. Crimson retains
its normalized thirteen-position down filter and nine-position tent
reconstruction. Godot Forward+ uses separable Gaussian reductions and separately
sampled selected levels. Equal numeric settings do not imply identical blur
shapes. Crimson's bounded Screen/Soft Light composition stays in linear
presentation space; Additive/Mix/Replace stay in exposed HDR.

Existing authored intensity, blend and level gains remain unchanged on load.
Omitted historical defaults are retained for old representations. Legacy
softness converts once in memory to `scale = 4 * threshold * softness`; writers
emit scale and cap only. This maps the authored transition width and does not
promise pixel-identical old extraction. Loading never rewrites or dirties a
scene. Individual reset arrows restore current defaults through normal history.

## End-to-end color-space findings

| Stage | Contract and audit result |
| --- | --- |
| HDR import | Original environment bytes are retained. Float HDR decodes as linear RGB. LDR environment sources decode sRGB once. The former 60000 radiance clamp was a fidelity defect and is removed in favor of a 1e30 numerical guard. |
| Environment bake/cache | Solid-angle source reductions, diffuse irradiance divided by pi and GGX convolution remain linear FP32. A 2k panorama selects 512-pixel cube faces. Cache preprocessing advances 4 to 5 so clipped cached radiance is rebuilt from the original source. |
| Material images | Color roles use one hardware sRGB decode; Data/Normal/Height remain linear. Color mips average in linear space then re-encode for storage. Generated float images remain linear. Alpha does not receive a gamma transform. |
| Material factors/colors | glTF and Quader material factors remain linear. HDR authoring swatches display sRGB without changing linear stored values on open. Emission and particle HDR colors retain their authored intensity. |
| Light colors | Scene light RGB is authored sRGB and decodes once during GPU preparation. Quader supplies linear light RGB; import now encodes it once to the SceneLight convention. Previously, linear .5 was incorrectly decoded to about .214. Existing scene values remain untouched until a normal reimport with Sync lights enabled. |
| Lighting/effects | PBR, environment light, GI, SSR and refraction accumulate/sample linear radiance. Redundant 60000 ceilings are removed from FP32 surface, fog and glass transmission output, including the ordinary/signed-SSR output mismatch. Actual RGBA16F GI donor/history/variance limits remain. Fog participates before scene Glow. Screen-space snapshots do not pass through tone mapping or sRGB encoding. Glow thresholds exposed HDR once; it does not feed light or GI. |
| Presentation | Tone mapping returns display-linear RGB. SMAA reconstructs linear color. `fs_encode.sc` applies the final sRGB transfer once. Display-encoded viewport/thumbnail images then pass through ordinary non-sRGB UI textures and UNORM native targets. DX11/DX12 platform copies add no transfer function. |

The supplied 2048x1024 HDRI contains RGB values up to 129536. Its 21 channels
above 60000 represented only seven pixels but about 20.5% of solid-angle
weighted source luminance. Clipping them changed both sharp sky/sun radiance and
the filtered lighting energy; a local Glow cap cannot repair that lost energy.
The cap therefore remains confined to the extracted Glow image.

The numerical audit also checks tone curves above the FP32 multiplication
overflow boundary. ACES uses a normalized rational fit, extended Reinhard divides
before multiplying its white factor, and AgX bounds its internal inset where
every supported SDR curve has already saturated. The CPU reference evaluates
the original equations in double precision. These changes preserve ordinary
reference values and the one-encoding contract without clipping the source HDRI
or changing direct-light brightness.

## Light defaults and interpretation

Crimson's ordinary point default is intensity 35, inverse-square attenuation,
radius 20 m, source radius zero and near falloff zero. Godot's default art-energy,
distance decay and range use a different convention; copying the intensity
number alone is not a valid energy match. A punctual light can produce very high
near-field radiance and narrow GGX highlights. This is separate from Glow's
threshold, cap and broad-scale gains. The audit does not rescale all authored
lights to hide a presentation problem.

Glow spreads visible image energy over geometry, as a lens effect does. The
opaque frame must first occlude the actual source; fully covered source pixels
must not continue emitting into the filter. A wide halo from still-visible
bright pixels is possible. The focused fixture distinguishes these cases with
nested 0/50/90/100 percent source coverage, finite HDR readback, linear gain
checks, threshold/cap response and exact effect-off output.

## Focused validation

The focused checks cover independent HDR source-energy conservation, values
above 60000 surviving decode/bake/cache, cache-version rejection, linear light
color publication, current and legacy Glow persistence, threshold/scale/cap GPU
response, extreme tone-curve input, and actual property-reset gestures with
undo/redo. Native presentation fixtures compare Scene, Game, previews and
thumbnails against independent CPU tone-curve values on DX12 and DX11.

The shadow-camera comparison uses an immutable disposable project snapshot and
preserved before binaries/shaders. Diagnostic imports and captures never modify
the authored scene or material files. See the generated local reports under
`build/Release/verification/hdr-glow-audit` for the captured settings and image
evidence. Thirty-three focused checks passed: twenty CPU/document/editor checks
and thirteen DX12/DX11 rendering checks, including shader reload and SMAA.
Release integration and material-cache preparation completed for all ten
topologies; the warm confirmation used 56 cached stages and no shader compiler
processes. The initial MSVC optimizer crash in a nested gradient-reset callback
was resolved with a simple named helper, without disabling optimization.

The three 1280x720 ShadowCamera images use exposure 1, 512 warmup draws, the same
draw frame 4608 and SSGI phase 512. The before/after pair has identical source
records and preserved authored settings. A third image changes only Glow in a
second disposable copy to show the current default profile. All source hashes,
camera/phase/readiness checks and staged-shader comparisons pass. These images
are evidence for this camera; the targeted source/coverage fixtures separately
test the reported solar-occlusion and extreme-HDR cases.
