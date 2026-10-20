# Height gradient stability checkpoint

This checkpoint records the 2026-10-20 investigation of elongated radial and
diagonal relief streaks on `spiral_stairs_walk_area`. It separates a Smooth
normal reconstruction defect from the finite geometric sampling of the Height
map. The Release build, seven focused checks, saved-project diagnostic captures
and matched GPU cost measurements are complete.

## Revisions and scope

- Previous shared renderer baseline:
  `6449dc6f8f15a2de2a0b35295621c62102c5e91b`.
- Updated shader source:
  `05cdc5598ac37e349919bcc04e2f3970bd405136`.
- Material ABI/generator remains **32**. This is a generated shader body/source
  change; ordinary cache source identity invalidates affected artifacts. There
  is no material or scene migration, vertex layout change, new material control,
  automatic Height LOD policy or dependency revision in this slice.
- Implementation and diagnostics do not write authored scene, material or image
  files. Saved-project diagnostics use disposable copies for comparison;
  concurrent user edits are retained rather than restored to a test baseline.

## Actual affected mesh and material

The saved scene entity is `spiral_stairs_walk_area`, entity ID
`1b88eaa8c22e318d`, model ID `c27dc7a1-0e0b-55a0-ac6a-c0b2bccf8937`.
Its native Quader source key is `mesh:122:122:0`, with mesh owner `object:122`.
The model belongs to the durable bundle
`Content/diablo_room/models/diablo_room_a8695214.crimson-quader`.

The decoded renderer input is the expanded `model.gltf` and `buffers/0.bin`
under
`.crimson/cache/quader-imports/a8695214-0c58-5bed-bd3b-77a26ce5f828/c27dc7a1-0e0b-55a0-ac6a-c0b2bccf8937/`.
The manifest assigns material ID `8bbc4fed-ae99-5080-a32b-e96dbe088c08`,
named `str_stoneflr1`. Its Height input uses a Linear Data texture node and
identity UV0 mapping. The Height image is
`Content/diablo_room/textures/heightmap.png`: 2048 × 2048, 16-bit grayscale,
with decoded values 4601–49369.

The entity authors **Smooth** (`meshShading: 2`). Its local transform is
translation `(7.5, 0.5, 6)`; its `Imported` and `Quader` ancestors have identity
transforms. Rotation, nonuniform scale and shear are absent in this example.

The user changed material values during the investigation. The completed old
renderer saved-project capture uses **Amount 0.06, Factor 20**. The CPU sampling
study below uses the earlier saved **Amount 0.046, Factor 40, Midpoint 0.398,
Height LOD 0**. These are distinct observations, not matching before/after
measurement configurations. Comparisons must preserve one frozen configuration
per pair rather than comparing those two authored states.

## Source topology evidence

Direct accessor/buffer decoding found:

| Property | Observed value |
| --- | ---: |
| Vertices | 1192 |
| Triangle indices | 1788 |
| Triangles | 596 |
| Four-vertex groups | 298 |
| Horizontal tread triangles | 400 |
| Side triangles | 196 |
| Distinct geometric positions | 450 |
| Positions with multiple authored normal charts | 403 |
| Positions with multiple UV values | 339 |

Every consecutive four-vertex group uses exactly six indices:
`(0, 1, 2, 0, 2, 3)` relative to that group's first vertex. Thus each source quad
arrives as **two triangles**, with a diagonal between its first and third
vertices. The current hardware tessellator consumes those triangle patches;
Quad UV interpolation does not change their primitive type.

All 400 horizontal tread triangles use UV interpolation mode 2, inverse-bilinear
Quad. All 196 side triangles use mode 0. Within each triangle, the mode and
correction coefficients agree across its three vertices. Authored normals have
unit length and are constant within each of the 596 triangles. The data does not
contain a mixed-mode triangle or a mismatched per-corner correction coefficient.
Hard normal boundaries between tread and side faces are expected geometry.

The source has thin radial tread triangles. Long-edge/altitude aspect ratios have
median 4.4816, 75th percentile 6.4100, and 90th percentile/worst approximately
16.9268. The smallest sine of the angle between the two original patch edges is
0.05951835, an angle of approximately 3.412 degrees. Recovering a perpendicular
gradient from such nearly parallel sampled derivatives can amplify their errors
by approximately 16.8 times.

A representative worst triangle is index **311**, vertices `(620, 622, 623)`:

| Corner | Local position | UV0 |
| --- | --- | --- |
| 0 | `(-1.129503, 0.100000, -1.202798)` | `(2.000000, 1.742230)` |
| 1 | `(-0.323607, 0.100000, -0.235114)` | `(3.250000, 2.052787)` |
| 2 | `(-0.273819, 0.100000, -0.291587)` | `(3.250000, 1.742230)` |

Its longest edge is 1.259318 m, shortest edge 0.075287 m and altitude 0.074398 m.
The inverse-bilinear coordinates are `(0,1,0,1)`, `(1,0,0,1)` and `(1,1,1,1)`;
the constant correction is approximately
`(-3.559526e-9, -0.75757581, 4.449407e-9, -0.23527093)`.

At its centroid, the former fixed `1/512` barycentric probes span approximately
5.08 and 5.01 height texels, with local radii 2.4596 and 2.4414 mm. Their
directions are nearly parallel. This supplies a concrete source-data case for
the normal reconstruction defect rather than attributing the result to a
missing Smooth checkbox or incorrect authored hard edges.

## Shader correction

The updated domain shader constructs orthogonal local probe directions from the
longest patch edge and its perpendicular in the source triangle plane. Matching
barycentric velocities retain their actual local lengths. This removes the
nearly parallel difference basis without dividing probe positions by a small
triangle altitude.

The initial bounded triangle-relative extent is reduced using active Height
texture dimensions and their corrected UV0/UV1 or triplanar material mappings.
The finer contributing mip supplies a conservative bound for fractional Height
LOD, clamped to the available mip chain. Precision floors limit further probe
shrinking. The affine base derivative is analytical; neighbor subtraction uses
displacement offsets. Four neighboring complete Height evaluations remain,
followed by the existing inverse-transpose normal transformation.

The center Height evaluation, authored Height LOD, positions, source-normal
displacement direction, silhouettes, collision and submission topology do not
change. The 1/16-texel target is a bounded gradient-accuracy choice, not a full
filtered derivative-map implementation. Bilinear Height derivatives can retain
texture-scale facets under sufficient magnification.

## Independent geometry sampling limitation

The CPU study uses the actual Quad-corrected UVs, the decoded 16-bit image and
bilinear repeat sampling. Along triangle 311's long edges at Factor 40, a
1.25-tile UV span gives approximately **64 height texels between tessellated
edge vertices**. An edge midpoint's true displaced height can therefore differ
substantially from interpolation between its tessellated endpoints.

For Amount 0.046, the edge from corner 0 to corner 1 gives:

| CPU reference | Median absolute height error | 95th percentile | Maximum |
| --- | ---: | ---: | ---: |
| Factor 40, LOD 0 | 1.34 mm | 5.89 mm | 6.43 mm |
| Factor 64, LOD 0 | 1.38 mm | 6.30 mm | 10.21 mm |
| Factor 40, box-filtered LOD 5 reference | 0.45 mm | 1.33 mm | 1.69 mm |

These are CPU midpoint interpolation errors on one actual source edge, not GPU
timings or a whole-image quality score. The LOD 5 row uses an in-memory
box-filtered reference and is not a claim about the exact runtime mip filter.
The nonmonotonic Factor 64 result is aliasing: denser samples can land on
different peaks while still missing intervening relief.

This limitation is separate from Smooth normal conditioning. Correct normals
cannot generate the missing geometric samples. Higher authored Height Texture
LOD trades relief detail for a lower-frequency geometric signal; suitable finer
source geometry supplies more samples. Disposable saved-project copies at LOD 0
and LOD 5 illustrate that tradeoff. They do not rewrite the authored scene,
material, image or bundle, and no automatic Height LOD policy is added here.

## Verification

The strict GPU regression run against the previous shader failed variant 0:
median angular error **4.427492 degrees**, 95th percentile **12.235502 degrees**,
against limits of **3 / 12 degrees**, respectively. This establishes that the
new independent reference can detect the old defect. This check measures
disagreement between cyclic control-point orders of the same surface, not
absolute angular error against an exact normal.

The updated DX12 fixture passed its existing silhouette, depth/picking, preview,
shadow, hard-edge, deformation and analytic normal contracts, plus all four
new thin-Quad variants. Each variant compares more than 4,500 interior pixels:

| Mapping / mip | Median order disagreement | 95th percentile |
| --- | ---: | ---: |
| Corrected UV0, LOD 0 | 0.013371 degrees | 1.255048 degrees |
| Corrected UV0 with scale/rotation/offset, LOD 0 | 0.019242 degrees | 1.429934 degrees |
| UV1, LOD 0 | 0.017615 degrees | 1.713017 degrees |
| Corrected UV0, LOD 2 | 0.014583 degrees | 1.372585 degrees |

Read-only actual-mesh captures before and after use Amount 0.06 and Factor 20,
the actual material graph, original geometry and complete production texture mip
chains. There are sixteen images per revision: overview/close views, Flat/Smooth,
Lit/Normal and diagnostic Height LOD 0/5. All eight Flat images match byte for
byte across revisions. This agrees with unchanged center displacement and
ordinary Flat shading.

The corrected Smooth LOD-0 image still shows substantial elongated detail.
Differential normals are evaluated at generated domain vertices and then
interpolated across rasterized triangles; high-frequency normals can therefore
alias alongside the geometric Height itself. The mathematical conditioning fix
does not establish a complete visual fix for this asset. The disposable LOD-5
copy substantially reduces the streaking, at the expense of fine geometric
relief. No diagnostic LOD or material copy is saved back to authored Content.

Local raw evidence lives under `build/Release/diagnostics/height-gradient-before/`
and `height-gradient-after/`; the latter includes `strict-dx12.log` and
`flat-comparison.json`. The strict portable regression report is
`build/Release/diagnostics/material-height/mesh-shading-correctness.json`.

## Integration

`Build.cmd Release` passed. Project cache preparation completed all eight
topologies, with 28 cached stages, eight compiler invocations and zero failures.
The existing DX12 fixture passed before the integration build against the same
frozen shader source. Six further focused checks passed after the build:

- `shader_preflight_core`, `shader_compiler_core`;
- `material_graph_shader_core`, `material_graph_publication_core`;
- `project_material_cache_core`, `material_height_render_dx11_core`.

No dependency revision or public API migration is required. Deploy the
rebuilt renderer/material-cache tools and matching generated shader family;
normal source hashing distinguishes the changed domain expressions.

## Performance

The saved baseline executable contains renderer `6449dc6`; the updated one
contains `05cdc55`. Both run the same existing mesh-shading benchmark with the
editor closed: owned DX12, NVIDIA GeForce RTX 4090 (`10de:2684`), driver
`32.0.16.1074`, 1920 x 1080. Static geometry has 512 source triangles and 289
vertices, a 64 x 64 RGBA8 linear Height map, Amount 0.02, Factor 8, one directional
and one point light. This is an isolated workload, not the stair scene's GPU cost.

After three seconds of common warmup, each phase warms for 240 frames and keeps
600 unique coherent completed GPU samples. Both executables run all twelve
ordinary/tessellated mode phases in forward/reverse order: 7,200 static samples
per revision. Only the new strict thin-Quad regression is skipped in benchmark
mode so that the intentionally failing old shader can complete the same timing
workload; ordinary CTest still runs that regression strictly. Existing correctness
contracts run in both benchmark executables.

Forward and reverse medians agree for every static mode:

| Opaque PBR view | Before median | After median | Added median |
| --- | ---: | ---: | ---: |
| Ordinary, each mode | 0.054272 ms | 0.054272 ms | 0 ms |
| Tessellated Use Mesh Normals | 0.092160 ms | 0.092160 ms | 0 ms |
| Tessellated Flat | 0.092160 ms | 0.092160 ms | 0 ms |
| Tessellated Smooth | 0.082944 ms | 0.084992 ms | 0.002048 ms |

Smooth's p95 rises from 0.086016 to 0.088064 ms in both phase directions.
The correction adds approximately 2.47% to that measured Smooth opaque pass,
while retaining the center plus four neighboring Height evaluations. The new
mapping arithmetic and texture-dimension queries have a measurable small cost
on this workload. These are GPU view timestamp elapsed durations, excluding
presentation, uploads and shadow work; they do not establish whole-editor FPS
or a universal cost for other geometry, factors, Height graphs or hardware.

The unchanged full benchmark also records six animated phases with 600 coherent
samples each. Those non-tessellated deformation/normal compute results are not
used to estimate the cost of this domain-stage change.

Raw phase samples are in
`build/Release/diagnostics/height-gradient-performance-before/mesh-shading-benchmark.json`
and the corresponding `height-gradient-performance-after/` report. The sibling
`height-gradient-performance-comparison.json` records phase deltas, and
`height-gradient-performance-provenance.json` records renderer/executable hashes
and adapter details.

## Remaining displacement pipeline work

The next renderer slice should pair footprint-filtered geometric Height with
fine per-fragment normal detail. Filtering geometry to the mapped spacing of
generated vertices prevents unsupported frequencies from becoming long relief
triangles. Reconstructing fine normals at the fragment footprint avoids relying
on interpolated sparse domain normals. Adjacent patches must agree on their
shared-edge Height sampling; independent constant per-patch mip choices can
introduce cracks. The authored Height LOD policy needs an explicit contract in
that future slice rather than silently replacing its current meaning here.

Per-fragment evaluation of a complete procedural Height graph several times can
be expensive. Image-only Height can instead use derived normal/gradient data;
general procedural and animated branches need their own measured path. Adaptive
edge factors and renderer-owned patch refinement can then add geometric samples
where relief or silhouettes justify them. A hardware Factor cap of 64 is not a
universal solution for a large source patch with many texture repetitions.

[GPU Gems 2, Chapter 7, Section 7.2.2](https://developer.nvidia.com/gpugems/gpugems2/part-i-geometric-complexity/chapter-7-adaptive-tessellation-subdivision-surfaces)
uses per-pixel normal mapping to retain displacement detail independently of
adaptive vertex density. These are proposed follow-up systems, not features
implemented by the current gradient-conditioning correction.

## Primary references

- [PBRT: Bump Mapping](https://pbr-book.org/3ed-2018/Materials/Bump_Mapping)
  derives the displaced surface differential, including changing original
  normals, and discusses finite-difference precision versus feature resolution.
- [Mikkelsen: Surface Gradient–Based Bump Mapping Framework, JCGT 2020](https://jcgt.org/published/0009/03/04/paper-lowres.pdf),
  Section 3.5, establishes the physical metric and texture-tiling requirements for
  displacement-consistent gradients. Section 4.7 distinguishes bilinear-height
  derivative faceting from filtered derivative reconstruction. Crimson's probe
  bounds are its own implementation choices.
- [HLSL SampleLevel](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-samplelevel)
  defines fractional mip interpolation and available-level clamping.
  [GetDimensions](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-getdimensions)
  exposes the dimensions and available mip count.
- The [shared material Height contract](../third_party/crimson-renderer/docs/MATERIAL_HEIGHT_DISPLACEMENT.md)
  and [Engine architecture](ENGINE_ARCHITECTURE.md#material-height-displacement)
  define current source ownership, sampling and publication behavior.
