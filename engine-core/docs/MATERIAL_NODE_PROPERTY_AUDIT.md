# Material node property audit

This audit follows an authored property through its editor binding, graph value,
evaluation path and published image or uniform. A changed hash alone does not
demonstrate that a control works. Image checks use asymmetric colored inputs,
non-flat height/normal fields and resolved procedural footprints; directional
checks test the advertised behavior where that direction is defined.

## Corrections

- Clouds 1/2 **Scale** controls feature size. Increasing it produces larger
  structures, with periodic sampling and continuous fractional edits. Scale 1
  retains the default. Earlier Clouds frequency values convert reciprocally
  while reading graph schemas 1–11; schema 12 writes the new meaning.
- Other procedural frequency controls are named **Density**, or **Tiling** for
  Parabolic Gradient. Higher values produce denser structures. Their authored
  values and equations retain their meaning.
- **Custom** procedural resolution opens editable Width/Height without a
  material edit until a dimension changes. Square-cell correction is inactive
  for square images and available for rectangular images.
- Broad **Blur Amount** retains fractional variance instead of rounding several
  adjacent slider values to the same integer-width box recipe. The bounded
  separable approximation uses the same two image planes. Rolling sums retain
  double precision through subtraction to avoid drifting impulse energy.
- A zero **Directional Warp Mask** copies the original pixel exactly, including
  non-power-of-two images, instead of introducing a redundant resampling step.
- Disconnected arithmetic **RGBA** inputs retain their type and expose RGB and
  Alpha in Properties, matching their inline controls.
- Variation help distinguishes the cyclic arrangement controls from White
  Noise's shifted grid.

## Coverage

| Node family | Properties and behavior checked | Evidence |
| --- | --- | --- |
| Scalar, Vector2, Constant, arithmetic | Components, typed fallback values, scalar broadcast, retained RGBA, operands and Lerp factor, uniform versus varying branches | `material_graph_core`; `material_graph_editor_core` |
| Levels and color adjustments | All five Levels controls, Brightness/Contrast, Desaturation, Contrast/Contrast RGB, Hue Shift, all Blend modes, Opacity and Mask, alpha preservation and zero/neutral identities | `material_graph_core` |
| Grayscale and channel nodes | Independent conversion equations on asymmetric RGBA, four independent Merge defaults, split outputs and straight alpha | `material_graph_core`; `material_graph_editor_core` |
| UV, Panner and Time | Each scale/offset axis, rotation, nonzero clock contexts, Speed, Pause/Period, mapping propagation; connected-input ownership | `material_graph_core`; existing `material_mapping_render_core`/DX11 checks |
| Texture 2D and Image Input | Color/Data interpretation, sampler transport and pixel cases, source/override/Base/Constant precedence, Enabled and retained references | `material_graph_core`; `material_atomic_image_bake_core`; `material_preview_resource_core` |
| Analytic Noise | Seed, Pattern, Feature size spatial identity, Clouds Detail and Contrast; unresolved mean | `material_graph_core` |
| Procedural Texture | Every advertised active family control changes resolved field pixels on a suitable fixture; Clouds structure growth, fractional continuity, scalar/prepared/batch parity, rectangular correction and periodicity | `material_procedural_core`; `material_procedural_properties_core` |
| Grunge | Twelve generation controls, independent layers, repeat edges, runtime-only Tiling and coverage endpoints | `material_graph_core`; `grunge_generator_core` |
| Resize, Blur, Grow/Shrink, Warp | Width/Height/Filter, sampling order, blur strength/variance/energy/fractional response, signed continuous morphology, warp Amount/Angle/mask | `material_resize_core`; `material_atomic_image_bake_core` |
| Mask Adjust and Histogram Scan | Coverage/Contrast, neutral values, endpoints and response direction | `material_atomic_image_bake_core` |
| Height/Normal conversion and occlusion | Strength, Detail size, Flip Y, Radius/Depth, zero-value behavior, all Direction Select axes and Both directions | `material_graph_core`; `material_atomic_image_bake_core`; existing mapping GPU checks |
| Image Transform | Scale, Rotation, Offset and completed-source sampling; UV1/triplanar mapping propagation | `material_atomic_image_bake_core`; `material_graph_core`; existing mapping GPU checks |
| Material Output | Shading/blend/coverage, material factors, emission, fog and tessellation field transport; renderer-specific appearance, native geometry and shadow coverage use their focused GPU fixtures | `material_graph_core`; existing `material_mapping_render_core`, `material_height_render_core`, `fog_background_render_core` |
| Editor transactions and previews | Numeric edits, one-step undo/redo, Escape cancellation, custom sizing, active/inactive fields, direct wiring, preview invalidation and warm resource reuse | `material_graph_editor_core`; `material_graph_actions_core`; `material_preview_resource_core` |

“Existing” GPU evidence describes the maintained focused fixtures, not a new
manual acceptance run or a claim that every control was visually inspected in
the running editor. Static image evaluation deliberately rejects surface Normal,
UV1/triplanar context and animated branches; those use renderer node previews.

## Intentional dependencies

A control can be meaningful without changing every possible input:

- Rotating circular Moisture kernels cannot change them; Width and Height must
  differ for orientation to have a visible effect.
- Fine Dirt becomes its filtered mean when Density makes both source bands
  smaller than the raster can resolve. At 1K this includes Density 10–32.
- Direction Select's signed axes match when Both directions is enabled.
- AO radii below one source pixel share a one-pixel minimum footprint.
- Spot Noise has no Clouds Detail control. A flat height field has no gradient
  to strengthen, and zero Strength intentionally clears the corresponding effect.
- Sampler minification requires minification, Alpha requires RGBA or coverage,
  connected inputs override their retained defaults, and Unlit materials do not
  use lit-only surface factors.
- Scalar slider bounds, linked-axis editing, names and positions describe editor
  behavior rather than additional material shading inputs.

These dependencies are separate from ignored setters, stale cache generations
and misleading property names. The audit corrects confirmed defects while
preserving meaningful filtering, type and coverage rules.

## Validation and measured cost

The final Release integration on 2026-10-21 passed all 15 selected checks:
graph math, actions, document persistence, Resize, procedural sources, the new
53-control procedural pixel matrix, atomic image operations, Grunge, headless
editor gestures, preview publication/resource lifetime, float texture streaming,
shader preflight, project material-cache compatibility and DX12 material mapping.
Material preparation reused all eight topologies and 32 cached stages with zero
compiler processes and zero failures. This is automated contract and rendering
evidence; user acceptance in the running editor remains separate.

Sequential CPU measurements used the same Ryzen 9 7900X machine as the
2026-10-20 baseline, with Crimson closed and no overlapping build/test work.
Each entry is a single cold run in seconds, not a statistical speedup claim.

| Operation | Size | Before | After |
| --- | --- | ---: | ---: |
| Clouds 1, default Scale 1 | 2K | 0.690 | 0.723 |
| Clouds 1, default Scale 1 | 4K | 2.724 | 2.630 |
| Clouds 2, default Scale 1 | 2K | 1.026 | 1.033 |
| Clouds 2, default Scale 1 | 4K | 4.069 | 3.897 |
| Broad RGBA Blur | 2K | 1.711 | 1.861 |
| Broad RGBA Blur | 4K | 6.776 | 7.225 |

The corrected fractional Blur response costs about 7–9% more cold CPU time in
this comparison. Its measured temporary image allocation remains 128 MiB at 2K
and 512 MiB at 4K. Cancellation completed in 27.5 ms from request setup, including
the benchmark's initial 10 ms delay. The other image-kernel measurements varied
between -4.9% and +3.0%; these single runs do not establish an improvement or
regression. All 36 default procedural cases (12 families at 1K/2K/4K) completed.
Heavy sources remain expensive: 4K Directional Scratches took 17.39 seconds,
compared with 16.58 seconds in the prior run.

Procedural times include preparation and the scalar raster; image-kernel times
include the static image evaluator. They exclude host image conversion, mip
generation, GPU uploads and frame rendering. The benchmarks are reproducible
with `material_procedural_tests --benchmark <report.json>` and
`material_atomic_image_bake_tests --benchmark <report.json>`. Completed images
remain cached; these changes add no frame-rendering pass or sampler binding.
No FPS improvement or GPU-time delta is inferred from the CPU measurements.

## Numerical image preview correction

The subsequent 2026-10-21 preview slice changes only flat image presentation.
Scalar and Vector2 previews display raw clamped numerical channels; RGB/RGBA
retain one sRGB color transform and straight-alpha checker composition. Grunge
Value previews reuse the canonical data texture, while RGB uses its color
display derivative. Contextual and animated renderer previews retain their
existing rendered-material presentation. The display key advances to
`material-node-image-v4`; graph schema 12, procedural algorithm revision 2,
canonical image keys and shader generator/ABI stay unchanged.

The diagnosis used the existing default 1024-square Clouds sources at Seed 0,
Scale 1, then area-reduced those exact samples to 256 square. The previous
display transfer raised Clouds 1's mean from 0.633 to 0.815 and reduced its
displayed standard deviation from 0.090 to 0.053. Clouds 2 changed from a raw
mean of 0.686 and deviation of 0.074 to displayed 0.845 and 0.041. Removing the
color transfer from numerical previews restores those raw values without
normalizing, sharpening or changing any material data.

Adobe documents both color-managed and Raw views in its
[2D View guide](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/workspace/2d-view/2d-view),
with mode-dependent thumbnail behavior described in
[Color Management](https://experienceleague.adobe.com/en/docs/substance-3d-designer/using/color-management/color-management).
Crimson's correction establishes an explicit numerical display convention;
it does not claim identical Designer noise equations or display settings.

The focused resource fixture inspects actual published RGBA16F display pixels:
Scalar 0.25/0.5/0.75, Vector2 XY, RGB 0.5 converting to approximately 0.735357,
RGBA checker composition, raw Grunge texture reuse, and unchanged canonical
image values/authored graph serialization. A disposable alternating CPU
microbenchmark (nine trials of 50 images) measured the display loop including
RGBA allocation/copy at 0.310 versus 0.038 ms for 128 square, and 1.399 versus
0.308 ms for 256 square. These are display-conversion timings only; they exclude
source generation, uploads, GPU work and frame rendering. Warm previews remain
cached and no additional source samples, passes or sampler bindings are added.

The final Release build and all six selected checks passed:
`material_graph_core`, `material_atomic_image_bake_core`,
`grunge_generator_core`, `material_graph_editor_core`,
`float_texture_streaming_core` and `material_preview_resource_core`.
Material-cache preparation reused eight topologies and 32 cached stages with
zero compiler processes and zero failures. The canonical renderer remains at
`3a54327db2812155abc65acd8e9e9fcea5cadbff`; this correction changes host preview
presentation only.
